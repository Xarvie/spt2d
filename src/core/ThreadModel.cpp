#include "ThreadModel.h"

#include <thread>
#include <chrono>
#include <iostream>
#include <condition_variable>

namespace spt {

class SingleThreadModel : public ThreadModel {
public:
    bool initialize(const ThreadConfig& config, std::unique_ptr<IGameLogic> logic) override {
        m_config = config;
        m_logic = std::move(logic);
        if (!m_logic->onInit()) {
            std::cerr << "[ThreadModel] GameLogic init failed" << std::endl;
            return false;
        }
        std::cout << "[ThreadModel] Single-threaded mode initialized" << std::endl;
        return true;
    }

    void shutdown() override {
        if (m_logic) {
            m_logic->onShutdown();
            m_logic.reset();
        }
    }

    void pushTouchEvent(const TouchEvent& event) override {
        m_pendingTouches.push_back(event);
    }

    void pushKeyEvent(const KeyEvent& event) override {
        m_pendingKeys.push_back(event);
    }

    void pushMouseEvent(const MouseEvent& event) override {
        m_pendingMice.push_back(event);
    }

    void onFrameBegin() override {
        auto now = std::chrono::high_resolution_clock::now();
        if (m_lastFrameTime.time_since_epoch().count() > 0) {
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
                now - m_lastFrameTime);
            m_currentWork.deltaTime = duration.count() / 1000000.0f;
        }
        m_lastFrameTime = now;
        m_currentWork.logicTime += m_currentWork.deltaTime;
        m_currentWork.frameIndex++;
    }

    GameWork* getRenderWork() override {
        if (m_logic && m_logic->isRunning()) {
            m_logic->onUpdate(m_currentWork.deltaTime, m_pendingTouches, m_pendingKeys, m_pendingMice);
            m_pendingTouches.clear();
            m_pendingKeys.clear();
            m_pendingMice.clear();

            m_logic->onRender(m_currentWork);
        }
        return &m_currentWork;
    }

    void onFrameEnd() override {
        m_currentWork.clear();
    }

    bool isRunning() const override {
        return m_logic && m_logic->isRunning();
    }

    bool isMultithreaded() const override {
        return false;
    }

    size_t pendingGpuTasks() const override {
        return m_currentWork.gpuTasks.size();
    }

    void executeGpuTasks() override {
        std::queue<GpuTask> tasks = m_currentWork.gpuTasks;
        while (!tasks.empty()) {
            auto& task = tasks.front();
            if (task) task();
            tasks.pop();
        }
    }

    const ThreadConfig& config() const override {
        return m_config;
    }

private:
    ThreadConfig m_config;
    std::unique_ptr<IGameLogic> m_logic;
    GameWork m_currentWork;
    std::vector<TouchEvent> m_pendingTouches;
    std::vector<KeyEvent> m_pendingKeys;
    std::vector<MouseEvent> m_pendingMice;
    std::chrono::high_resolution_clock::time_point m_lastFrameTime;
};

class MultiThreadModel : public ThreadModel {
public:
    bool initialize(const ThreadConfig& config, std::unique_ptr<IGameLogic> logic) override {
        m_config = config;
        m_logic = std::move(logic);

        if (!m_logic->onInit()) {
            std::cerr << "[ThreadModel] GameLogic init failed" << std::endl;
            return false;
        }

        m_running = true;
        m_logicThread = std::thread(&MultiThreadModel::logicLoop, this);

        std::cout << "[ThreadModel] Multi-threaded Mode initialized (logic: " 
                  << m_config.logicFps << " fps, render: " 
                  << m_config.renderFps << " fps)" << std::endl;
        return true;
    }

    void shutdown() override {
        m_running = false;
        notifyLogicThread();

        if (m_logicThread.joinable()) {
            m_logicThread.join();
        }
    }

    void pushTouchEvent(const TouchEvent& event) override {
        m_touchQueue.push(event);
        notifyLogicThread();
    }

    void pushKeyEvent(const KeyEvent& event) override {
        m_keyQueue.push(event);
        notifyLogicThread();
    }

    void pushMouseEvent(const MouseEvent& event) override {
        m_mouseQueue.push(event);
        notifyLogicThread();
    }

    void onFrameBegin() override {
    }

    GameWork* getRenderWork() override {
        return m_workBuffer.getReadBuffer();
    }

    void onFrameEnd() override {
    }

    bool isRunning() const override {
        return m_running && m_logic && m_logic->isRunning();
    }

    bool isMultithreaded() const override {
        return true;
    }

    size_t pendingGpuTasks() const override {
        auto* work = m_workBuffer.getReadBuffer();
        return work ? work->gpuTasks.size() : 0;
    }

    void executeGpuTasks() override {
        auto* work = m_workBuffer.getReadBuffer();
        if (!work) return;

        std::queue<GpuTask> tasks = work->gpuTasks;
        while (!tasks.empty()) {
            auto& task = tasks.front();
            if (task) task();
            tasks.pop();
        }
    }

    const ThreadConfig& config() const override {
        return m_config;
    }

private:
    void logicLoop() {
        std::cout << "[LogicThread] Starting..." << std::endl;

        const auto targetDuration = std::chrono::microseconds(1000000 / m_config.logicFps);
        const float fixedDt = 1.0f / m_config.logicFps;

        auto nextTick = std::chrono::high_resolution_clock::now();
        double logicTime = 0.0;
        uint64_t frameIndex = 0;

        std::queue<TouchEvent> localTouches;
        std::queue<KeyEvent> localKeys;
        std::queue<MouseEvent> localMice;
        std::vector<TouchEvent> touchVec;
        std::vector<KeyEvent> keyVec;
        std::vector<MouseEvent> mouseVec;

        while (m_running) {
            auto now = std::chrono::high_resolution_clock::now();

            m_touchQueue.tryPopAll(localTouches);
            m_keyQueue.tryPopAll(localKeys);
            m_mouseQueue.tryPopAll(localMice);

            while (!localTouches.empty()) {
                touchVec.push_back(localTouches.front());
                localTouches.pop();
            }
            while (!localKeys.empty()) {
                keyVec.push_back(localKeys.front());
                localKeys.pop();
            }
            while (!localMice.empty()) {
                mouseVec.push_back(localMice.front());
                localMice.pop();
            }

            if (now >= nextTick) {
                nextTick += targetDuration;

                if (now > nextTick + std::chrono::milliseconds(200)) {
                    nextTick = now + targetDuration;
                }

                logicTime += fixedDt;
                frameIndex++;

                if (m_logic) {
                    m_logic->onUpdate(fixedDt, touchVec, keyVec, mouseVec);
                    touchVec.clear();
                    keyVec.clear();
                    mouseVec.clear();

                    GameWork* buffer = m_workBuffer.getWriteBuffer();
                    buffer->clear();
                    buffer->logicTime = logicTime;
                    buffer->deltaTime = fixedDt;
                    buffer->frameIndex = frameIndex;
                    
                    m_logic->onRender(*buffer);
                    m_workBuffer.publish();

                    if (!m_logic->isRunning()) {
                        m_running = false;
                    }
                }
            }

            {
                std::unique_lock<std::mutex> lock(m_waitMutex);
                now = std::chrono::high_resolution_clock::now();

                if (now < nextTick) {
                    m_waitCV.wait_until(lock, nextTick, [this] {
                        return m_notified.load() || !m_running;
                    });
                    m_notified = false;
                } else {
                    if (m_running) std::this_thread::yield();
                }
            }
        }

        if (m_logic) {
            m_logic->onShutdown();
            m_logic.reset();
        }

        std::cout << "[LogicThread] Stopped" << std::endl;
    }

    void notifyLogicThread() {
        {
            std::lock_guard<std::mutex> lock(m_waitMutex);
            m_notified = true;
        }
        m_waitCV.notify_one();
    }

    ThreadConfig m_config;
    std::unique_ptr<IGameLogic> m_logic;
    std::atomic<bool> m_running{false};
    std::thread m_logicThread;

    std::mutex m_waitMutex;
    std::condition_variable m_waitCV;
    std::atomic<bool> m_notified{false};

    TouchEventQueue m_touchQueue;
    KeyEventQueue m_keyQueue;
    MouseEventQueue m_mouseQueue;
    DoubleBuffer<GameWork> m_workBuffer;
};

std::unique_ptr<ThreadModel> ThreadModel::create(const ThreadConfig& config) {
    if (config.multithreaded) {
        return std::make_unique<MultiThreadModel>();
    } else {
        return std::make_unique<SingleThreadModel>();
    }
}

}
