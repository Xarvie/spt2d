#include "ThreadModel.h"
#include "TripleBuffer.h"
#include "Signal.h"

#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <iostream>

namespace spt3d {

class SingleThreadModel : public ThreadModel {
public:
    ~SingleThreadModel() override = default;

    bool initialize(const ThreadConfig&         config,
                    std::unique_ptr<IGameLogic> logic) override {
        m_config = config;
        m_logic  = std::move(logic);

        if (!m_logic) {
            std::cerr << "[SingleThreadModel] No game logic provided" << std::endl;
            return false;
        }

        if (!m_logic->onInit()) {
            std::cerr << "[SingleThreadModel] Game logic init failed" << std::endl;
            return false;
        }

        m_running = true;
        std::cout << "[SingleThreadModel] Initialized (single-threaded mode)" << std::endl;
        return true;
    }

    void shutdown() override {
        m_running = false;
        if (m_logic) {
            m_logic->onShutdown();
        }
        m_logic.reset();
        std::cout << "[SingleThreadModel] Shutdown complete" << std::endl;
    }

    void pushTouchEvent(const TouchEvent& e) override {
        std::lock_guard<std::mutex> lock(m_inputMutex);
        m_touchEvents.push_back(e);
    }

    void pushKeyEvent(const KeyEvent& e) override {
        std::lock_guard<std::mutex> lock(m_inputMutex);
        m_keyEvents.push_back(e);
    }

    void pushMouseEvent(const MouseEvent& e) override {
        std::lock_guard<std::mutex> lock(m_inputMutex);
        m_mouseEvents.push_back(e);
    }

    void onFrameBegin() override {
        auto now = std::chrono::high_resolution_clock::now();
        if (m_lastFrameTime.time_since_epoch().count() > 0) {
            auto elapsed = std::chrono::duration<float>(now - m_lastFrameTime).count();
            m_frameStats.renderDeltaTime = elapsed;
        }
        m_lastFrameTime = now;

        {
            std::lock_guard<std::mutex> lock(m_inputMutex);
            m_inputFrame.touches = &m_touchEvents;
            m_inputFrame.keys    = &m_keyEvents;
            m_inputFrame.mice    = &m_mouseEvents;
        }

        if (m_logic && m_running) {
            m_logic->onUpdate(m_frameStats.renderDeltaTime, m_inputFrame);
        }

        {
            std::lock_guard<std::mutex> lock(m_inputMutex);
            m_touchEvents.clear();
            m_keyEvents.clear();
            m_mouseEvents.clear();
        }
        m_inputFrame.touches = nullptr;
        m_inputFrame.keys    = nullptr;
        m_inputFrame.mice    = nullptr;

        m_work.reset();
        if (m_logic && m_running) {
            m_logic->onRender(m_work);
        }
    }

    const GameWork* getRenderWork() override {
        return &m_work;
    }

    void onFrameEnd() override {
        m_frameStats.frameIndex++;
    }

    bool isRunning() const override {
        return m_running && m_logic && m_logic->isRunning();
    }

    bool isMultithreaded() const override {
        return false;
    }

    FrameStats frameStats() const override {
        return m_frameStats;
    }

    const ThreadConfig& config() const override {
        return m_config;
    }

private:
    ThreadConfig               m_config;
    std::unique_ptr<IGameLogic> m_logic;
    std::atomic<bool>          m_running{false};

    std::mutex                 m_inputMutex;
    std::vector<TouchEvent>    m_touchEvents;
    std::vector<KeyEvent>      m_keyEvents;
    std::vector<MouseEvent>    m_mouseEvents;
    InputFrame                 m_inputFrame;

    GameWork                   m_work;
    FrameStats                 m_frameStats;
    std::chrono::high_resolution_clock::time_point m_lastFrameTime;
};

class MultiThreadModel : public ThreadModel {
public:
    ~MultiThreadModel() override {
        shutdown();
    }

    bool initialize(const ThreadConfig&         config,
                    std::unique_ptr<IGameLogic> logic) override {
        m_config = config;
        m_logic  = std::move(logic);

        if (!m_logic) {
            std::cerr << "[MultiThreadModel] No game logic provided" << std::endl;
            return false;
        }

        if (!m_logic->onInit()) {
            std::cerr << "[MultiThreadModel] Game logic init failed" << std::endl;
            return false;
        }

        m_running = true;
        m_logicThread = std::thread(&MultiThreadModel::logicLoop, this);

        std::cout << "[MultiThreadModel] Initialized (multi-threaded mode)" << std::endl;
        return true;
    }

    void shutdown() override {
        m_running = false;
        m_cv.notify_all();

        if (m_logicThread.joinable()) {
            m_logicThread.join();
        }

        if (m_logic) {
            m_logic->onShutdown();
        }
        m_logic.reset();
        std::cout << "[MultiThreadModel] Shutdown complete" << std::endl;
    }

    void pushTouchEvent(const TouchEvent& e) override {
        std::lock_guard<std::mutex> lock(m_inputMutex);
        m_pendingTouches.push_back(e);
    }

    void pushKeyEvent(const KeyEvent& e) override {
        std::lock_guard<std::mutex> lock(m_inputMutex);
        m_pendingKeys.push_back(e);
    }

    void pushMouseEvent(const MouseEvent& e) override {
        std::lock_guard<std::mutex> lock(m_inputMutex);
        m_pendingMice.push_back(e);
    }

    void onFrameBegin() override {
        auto now = std::chrono::high_resolution_clock::now();
        if (m_lastRenderTime.time_since_epoch().count() > 0) {
            m_frameStats.renderDeltaTime = std::chrono::duration<float>(now - m_lastRenderTime).count();
        }
        m_lastRenderTime = now;

        m_workBuffer.fetch();
        m_renderWork = &m_workBuffer.readBuffer();
    }

    const GameWork* getRenderWork() override {
        return m_renderWork;
    }

    void onFrameEnd() override {
        m_frameStats.frameIndex++;
        m_renderReady = true;
        m_cv.notify_one();
    }

    bool isRunning() const override {
        return m_running && m_logic && m_logic->isRunning();
    }

    bool isMultithreaded() const override {
        return true;
    }

    FrameStats frameStats() const override {
        return m_frameStats;
    }

    const ThreadConfig& config() const override {
        return m_config;
    }

private:
    void logicLoop() {
        auto lastTime = std::chrono::high_resolution_clock::now();

        while (m_running) {
            {
                std::unique_lock<std::mutex> lock(m_syncMutex);
                m_cv.wait(lock, [this] {
                    return m_renderReady || !m_running;
                });
                if (!m_running) break;
                m_renderReady = false;
            }

            auto now = std::chrono::high_resolution_clock::now();
            float dt = std::chrono::duration<float>(now - lastTime).count();
            lastTime = now;

            m_frameStats.logicDeltaTime = dt;

            InputFrame input;
            {
                std::lock_guard<std::mutex> lock(m_inputMutex);
                input.touches = &m_pendingTouches;
                input.keys    = &m_pendingKeys;
                input.mice    = &m_pendingMice;
            }

            if (m_logic && m_running) {
                m_logic->onUpdate(dt, input);
            }

            {
                std::lock_guard<std::mutex> lock(m_inputMutex);
                m_pendingTouches.clear();
                m_pendingKeys.clear();
                m_pendingMice.clear();
            }

            GameWork& work = m_workBuffer.writeBuffer();
            work.reset();
            work.frameIndex = m_frameStats.frameIndex;
            work.deltaTime  = dt;
            
            if (m_logic && m_running) {
                m_logic->onRender(work);
            }
            
            m_workBuffer.publish();
        }
    }

    ThreadConfig                m_config;
    std::unique_ptr<IGameLogic> m_logic;
    std::atomic<bool>           m_running{false};
    std::thread                 m_logicThread;

    std::mutex                  m_inputMutex;
    std::vector<TouchEvent>     m_pendingTouches;
    std::vector<KeyEvent>       m_pendingKeys;
    std::vector<MouseEvent>     m_pendingMice;

    TripleBuffer<GameWork>      m_workBuffer;
    const GameWork*             m_renderWork = nullptr;

    std::mutex                  m_syncMutex;
    std::condition_variable     m_cv;
    bool                        m_renderReady = true;

    FrameStats                  m_frameStats;
    std::chrono::high_resolution_clock::time_point m_lastRenderTime;
};

std::unique_ptr<ThreadModel> ThreadModel::create(const ThreadConfig& config) {
    if (config.multithreaded) {
        return std::make_unique<MultiThreadModel>();
    }
    return std::make_unique<SingleThreadModel>();
}

} // namespace spt3d
