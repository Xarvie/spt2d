#include "../Spt3D.h"
#include "ThreadModel.h"
#include "DoubleBuffer.h"
#include "../platform/Platform.h"
#include "../vfs/VirtualFileSystem.h"

#include <chrono>
#include <atomic>
#include <iostream>
#include <cassert>
#include <thread>

namespace spt3d {

namespace {
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
}

static struct {
    spt3d::ThreadConfig            threadConfig;
    std::unique_ptr<spt3d::ThreadModel> threadModel;
    std::unique_ptr<spt3d::IPlatformHub> platform;
    spt3d::IPlatformHub* platformPtr = nullptr;
    
    TimePoint startTime;
    TimePoint lastFrameTime;

    // --- Timing fields: written by the frame-pump thread, read from any thread.
    // Using atomics avoids data races without a heavy mutex on every Delta()/Time() call.
    std::atomic<float>  deltaTime{0.0f};
    std::atomic<double> totalTime{0.0};
    std::atomic<int>    fps{0};

    // --- Frame-pump internal (only accessed by the pumping thread, not shared)
    int frameCount = 0;
    float fpsAccum = 0.0f;
    
    bool initialized = false;
    std::atomic<bool> shouldQuit{false};
} g_state;

void SetThreadConfig(const ThreadConfig& cfg) {
    g_state.threadConfig = spt3d::ThreadConfig();
    g_state.threadConfig.multithreaded = cfg.multithreaded;
    g_state.threadConfig.logicHz = cfg.logicHz;
    g_state.threadConfig.renderHz = cfg.renderHz;
    g_state.threadConfig.maxDeltaTimeSeconds = cfg.maxDeltaTimeSeconds;
    g_state.threadConfig.logicThreadName = cfg.logicThreadName;
    g_state.threadConfig.renderThreadName = cfg.renderThreadName;
}

ThreadConfig GetThreadConfig() {
    ThreadConfig cfg;
    cfg.multithreaded = g_state.threadConfig.multithreaded;
    cfg.logicHz = g_state.threadConfig.logicHz;
    cfg.renderHz = g_state.threadConfig.renderHz;
    cfg.maxDeltaTimeSeconds = g_state.threadConfig.maxDeltaTimeSeconds;
    cfg.logicThreadName = g_state.threadConfig.logicThreadName;
    cfg.renderThreadName = g_state.threadConfig.renderThreadName;
    return cfg;
}

class GameLogicAdapter : public spt3d::IGameLogic {
public:
    std::unique_ptr<spt3d::IGameLogic> userLogic;
    
    bool onInit() override {
        return userLogic ? userLogic->onInit() : true;
    }
    
    void onUpdate(float dt, const spt3d::InputFrame& input) override {
        if (userLogic) {
            userLogic->onUpdate(dt, input);
        }
    }
    
    void onRender(spt3d::GameWork& work) override {
        if (userLogic) {
            userLogic->onRender(work);
        }
    }
    
    void onShutdown() override {
        if (userLogic) {
            userLogic->onShutdown();
        }
    }
    
    bool isRunning() const override {
        return userLogic ? userLogic->isRunning() : false;
    }
};

bool Init(const AppConfig& cfg, std::unique_ptr<IGameLogic> game) {
    if (g_state.initialized) {
        std::cerr << "[spt3d] Already initialized\n";
        return false;
    }
    
    g_state.startTime = Clock::now();
    g_state.lastFrameTime = g_state.startTime;
    
    spt3d::PlatformConfig platCfg;
    platCfg.width = cfg.width;
    platCfg.height = cfg.height;
    platCfg.title = std::string(cfg.title);
    platCfg.targetFps = 60;
    
#if defined(__WXGAME__)
    g_state.platform = spt3d::createPlatformWx();
#elif defined(EMSCRIPTEN)
    g_state.platform = spt3d::createPlatformWeb();
#else
    g_state.platform = spt3d::createPlatformSdl3();
#endif
    
    if (!g_state.platform || !g_state.platform->initialize(platCfg)) {
        std::cerr << "[spt3d] Platform initialization failed\n";
        return false;
    }
    g_state.platformPtr = g_state.platform.get();
    
    auto adapter = std::make_unique<GameLogicAdapter>();
    adapter->userLogic = std::move(game);
    
    g_state.threadModel = spt3d::ThreadModel::create(g_state.threadConfig);
    if (!g_state.threadModel->initialize(g_state.threadConfig, std::move(adapter))) {
        std::cerr << "[spt3d] ThreadModel initialization failed\n";
        return false;
    }
    
    g_state.initialized = true;
    std::cout << "[spt3d] Initialized (" 
              << (g_state.threadConfig.multithreaded ? "multi" : "single") 
              << "-threaded)\n";
    return true;
}

bool Init(const AppConfig& cfg) {
    return Init(cfg, std::unique_ptr<IGameLogic>());
}

void Run() {
    if (!g_state.initialized || !g_state.threadModel) {
        std::cerr << "[spt3d] Not initialized\n";
        return;
    }
    
    while (g_state.threadModel->isRunning() && !g_state.shouldQuit) {
        // Process pending VFS (async file load) callbacks each frame.
        spt3d::VirtualFileSystem::Instance().processCompleted();
        
        g_state.threadModel->onFrameBegin();
        
        const spt3d::GameWork* work = g_state.threadModel->getRenderWork();
        if (work) {
            if (g_state.platformPtr) {
                auto window = g_state.platformPtr->getWindowSystem();
                if (window) {
                    window->swapBuffers();
                }
            }
        }
        
        g_state.threadModel->onFrameEnd();
        
        updateFrameTiming();
    }
}

void Run(std::function<void()> tick, int target_fps) {
    if (!g_state.initialized) {
        std::cerr << "[spt3d] Not initialized\n";
        return;
    }
    
    auto frameDuration = target_fps > 0 
        ? std::chrono::microseconds(1000000 / target_fps) 
        : std::chrono::microseconds(0);
    
    while (!g_state.shouldQuit) {
        auto frameStart = Clock::now();
        
        // Process pending VFS (async file load) callbacks each frame.
        spt3d::VirtualFileSystem::Instance().processCompleted();
        
        if (tick) tick();
        
        if (g_state.platformPtr) {
            auto window = g_state.platformPtr->getWindowSystem();
            if (window) {
                window->swapBuffers();
            }
        }
        
        updateFrameTiming();
        
        if (frameDuration.count() > 0) {
            auto elapsed = Clock::now() - frameStart;
            if (elapsed < frameDuration) {
                std::this_thread::sleep_for(frameDuration - elapsed);
            }
        }
    }
}

void Shutdown() {
    if (!g_state.initialized) return;
    
    if (g_state.threadModel) {
        g_state.threadModel->shutdown();
        g_state.threadModel.reset();
    }
    
    if (g_state.platform) {
        g_state.platform->shutdown();
        g_state.platform.reset();
    }
    
    g_state.platformPtr = nullptr;
    g_state.initialized = false;
    std::cout << "[spt3d] Shutdown complete\n";
}

void BeginFrame() {
    spt3d::VirtualFileSystem::Instance().processCompleted();
    g_state.lastFrameTime = Clock::now();
}

// Helper: updates the shared timing atomics from the frame-pump thread.
// lastFrameTime, frameCount, fpsAccum are frame-pump-private (non-atomic).
static void updateFrameTiming() {
    auto now = Clock::now();
    float dt = std::chrono::duration<float>(now - g_state.lastFrameTime).count();
    g_state.lastFrameTime = now;

    g_state.deltaTime.store(dt, std::memory_order_relaxed);
    g_state.totalTime.store(g_state.totalTime.load(std::memory_order_relaxed)
                            + static_cast<double>(dt),
                            std::memory_order_relaxed);

    g_state.frameCount++;
    g_state.fpsAccum += dt;
    if (g_state.fpsAccum >= 1.0f) {
        g_state.fps.store(g_state.frameCount, std::memory_order_relaxed);
        g_state.frameCount = 0;
        g_state.fpsAccum = 0.0f;
    }
}

void EndFrame() {
    updateFrameTiming();
}

void Quit() {
    g_state.shouldQuit.store(true, std::memory_order_relaxed);
}

bool ShouldQuit() {
    return g_state.shouldQuit.load(std::memory_order_relaxed);
}

float Delta() {
    return g_state.deltaTime.load(std::memory_order_relaxed);
}

double Time() {
    return g_state.totalTime.load(std::memory_order_relaxed);
}

int FPS() {
    return g_state.fps.load(std::memory_order_relaxed);
}

void SetTargetFPS(int fps) {
    (void)fps;
}

GameWork& CurrentWork() {
    static GameWork s_work;
    return s_work;
}

int ScreenW() {
    if (g_state.platformPtr) {
        auto window = g_state.platformPtr->getWindowSystem();
        if (window) {
            return window->getWindowInfo().screenWidth;
        }
    }
    return 0;
}

int ScreenH() {
    if (g_state.platformPtr) {
        auto window = g_state.platformPtr->getWindowSystem();
        if (window) {
            return window->getWindowInfo().screenHeight;
        }
    }
    return 0;
}

int RenderW() {
    return ScreenW();
}

int RenderH() {
    return ScreenH();
}

float DPR() {
    if (g_state.platformPtr) {
        auto window = g_state.platformPtr->getWindowSystem();
        if (window) {
            return window->getWindowInfo().pixelRatio;
        }
    }
    return 1.0f;
}

void Resize(int w, int h) {
    (void)w; (void)h;
}

bool Resized() {
    return false;
}

bool Focused() {
    return true;
}

bool IsFullscreen() {
    return false;
}

void ToggleFullscreen() {
}

Signal<int, int>& OnResize() {
    static Signal<int, int> sig;
    return sig;
}

Signal<>& OnFocusGain() {
    static Signal<> sig;
    return sig;
}

Signal<>& OnFocusLost() {
    static Signal<> sig;
    return sig;
}

Signal<>& OnQuit() {
    static Signal<> sig;
    return sig;
}

Signal<std::string_view>& OnDropFile() {
    static Signal<std::string_view> sig;
    return sig;
}

}
