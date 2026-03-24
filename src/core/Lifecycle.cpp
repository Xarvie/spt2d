#include "../Spt3D.h"
#include "ThreadModel.h"
#include "DoubleBuffer.h"
#include "../platform/Platform.h"

#include <chrono>
#include <iostream>
#include <cassert>
#include <thread>

namespace spt3d {

namespace {
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
}

static struct {
    spt::ThreadConfig            threadConfig;
    std::unique_ptr<spt::ThreadModel> threadModel;
    std::unique_ptr<spt::IPlatformHub> platform;
    spt::IPlatformHub* platformPtr = nullptr;
    
    TimePoint startTime;
    TimePoint lastFrameTime;
    float deltaTime = 0.0f;
    double totalTime = 0.0;
    int fps = 0;
    int frameCount = 0;
    float fpsAccum = 0.0f;
    
    bool initialized = false;
    bool shouldQuit = false;
} g_state;

ThreadConfig ThreadConfig::SingleThread(int hz) {
    ThreadConfig c;
    c.multithreaded = false;
    c.logicHz = hz;
    c.renderHz = hz;
    c.logicFpsMode = FrameRateMode::FixedHz;
    c.renderFpsMode = FrameRateMode::FixedHz;
    return c;
}

ThreadConfig ThreadConfig::MultiThread(int logicHz, int renderHz) {
    ThreadConfig c;
    c.multithreaded = true;
    c.logicHz = logicHz;
    c.renderHz = renderHz;
    c.logicFpsMode = FrameRateMode::FixedHz;
    c.renderFpsMode = FrameRateMode::FixedHz;
    return c;
}

void SetThreadConfig(const ThreadConfig& cfg) {
    g_state.threadConfig = spt::ThreadConfig();
    g_state.threadConfig.multithreaded = cfg.multithreaded;
    g_state.threadConfig.logicHz = cfg.logicHz;
    g_state.threadConfig.renderHz = cfg.renderHz;
    g_state.threadConfig.maxDeltaTimeSeconds = cfg.maxDeltaTime;
    g_state.threadConfig.logicThreadName = cfg.logicThreadName;
    g_state.threadConfig.renderThreadName = cfg.renderThreadName;
}

ThreadConfig GetThreadConfig() {
    ThreadConfig cfg;
    cfg.multithreaded = g_state.threadConfig.multithreaded;
    cfg.logicHz = g_state.threadConfig.logicHz;
    cfg.renderHz = g_state.threadConfig.renderHz;
    cfg.maxDeltaTime = g_state.threadConfig.maxDeltaTimeSeconds;
    cfg.logicThreadName = g_state.threadConfig.logicThreadName;
    cfg.renderThreadName = g_state.threadConfig.renderThreadName;
    return cfg;
}

class GameLogicAdapter : public spt::IGameLogic {
public:
    std::unique_ptr<spt3d::IGameLogic> userLogic;
    
    bool onInit() override {
        return userLogic ? userLogic->OnInit() : true;
    }
    
    void onUpdate(float dt, const spt::InputFrame& input) override {
        if (userLogic) {
            spt3d::InputFrame adapted;
            static std::vector<spt3d::TouchEvent> touches;
            static std::vector<spt3d::KeyEvent> keys;
            static std::vector<spt3d::MouseEvent> mice;
            
            if (input.touches) {
                touches.clear();
                for (const auto& t : *input.touches) {
                    spt3d::TouchEvent te;
                    te.type = static_cast<spt3d::TouchEvent::Type>(t.type);
                    te.id = t.id;
                    te.x = t.x;
                    te.y = t.y;
                    touches.push_back(te);
                }
                adapted.touches = &touches;
            }
            if (input.keys) {
                keys.clear();
                for (const auto& k : *input.keys) {
                    spt3d::KeyEvent ke;
                    ke.type = static_cast<spt3d::KeyEvent::Type>(k.type);
                    ke.code = 0;
                    keys.push_back(ke);
                }
                adapted.keys = &keys;
            }
            if (input.mice) {
                mice.clear();
                for (const auto& m : *input.mice) {
                    spt3d::MouseEvent me;
                    me.x = m.x;
                    me.y = m.y;
                    me.deltaX = m.deltaX;
                    me.deltaY = m.deltaY;
                    me.leftButton = (m.button == 1);
                    me.rightButton = (m.button == 2);
                    mice.push_back(me);
                }
                adapted.mice = &mice;
            }
            userLogic->OnUpdate(dt, adapted);
        }
    }
    
    void onRender(spt::GameWork& work) override {
        (void)work;
        if (userLogic) {
            userLogic->OnRender();
        }
    }
    
    void onShutdown() override {
        if (userLogic) {
            userLogic->OnShutdown();
        }
    }
    
    bool isRunning() const override {
        return userLogic ? userLogic->IsRunning() : false;
    }
};

bool Init(const AppConfig& cfg, std::unique_ptr<IGameLogic> game) {
    if (g_state.initialized) {
        std::cerr << "[spt3d] Already initialized\n";
        return false;
    }
    
    g_state.startTime = Clock::now();
    g_state.lastFrameTime = g_state.startTime;
    
    spt::PlatformConfig platCfg;
    platCfg.width = cfg.width;
    platCfg.height = cfg.height;
    platCfg.title = std::string(cfg.title);
    platCfg.targetFps = 60;
    
#if defined(__WXGAME__)
    g_state.platform = spt::createPlatformWx();
#elif defined(EMSCRIPTEN)
    g_state.platform = spt::createPlatformWeb();
#else
    g_state.platform = spt::createPlatformSdl3();
#endif
    
    if (!g_state.platform || !g_state.platform->initialize(platCfg)) {
        std::cerr << "[spt3d] Platform initialization failed\n";
        return false;
    }
    g_state.platformPtr = g_state.platform.get();
    
    auto adapter = std::make_unique<GameLogicAdapter>();
    adapter->userLogic = std::move(game);
    
    g_state.threadModel = spt::ThreadModel::create(g_state.threadConfig);
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
        g_state.threadModel->onFrameBegin();
        
        const spt::GameWork* work = g_state.threadModel->getRenderWork();
        if (work) {
            if (g_state.platformPtr) {
                auto window = g_state.platformPtr->getWindowSystem();
                if (window) {
                    window->swapBuffers();
                }
            }
        }
        
        g_state.threadModel->onFrameEnd();
        
        auto now = Clock::now();
        g_state.deltaTime = std::chrono::duration<float>(now - g_state.lastFrameTime).count();
        g_state.lastFrameTime = now;
        g_state.totalTime += g_state.deltaTime;
        
        g_state.frameCount++;
        g_state.fpsAccum += g_state.deltaTime;
        if (g_state.fpsAccum >= 1.0f) {
            g_state.fps = g_state.frameCount;
            g_state.frameCount = 0;
            g_state.fpsAccum = 0.0f;
        }
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
        
        if (tick) tick();
        
        if (g_state.platformPtr) {
            auto window = g_state.platformPtr->getWindowSystem();
            if (window) {
                window->swapBuffers();
            }
        }
        
        auto now = Clock::now();
        g_state.deltaTime = std::chrono::duration<float>(now - g_state.lastFrameTime).count();
        g_state.lastFrameTime = now;
        g_state.totalTime += g_state.deltaTime;
        
        g_state.frameCount++;
        g_state.fpsAccum += g_state.deltaTime;
        if (g_state.fpsAccum >= 1.0f) {
            g_state.fps = g_state.frameCount;
            g_state.frameCount = 0;
            g_state.fpsAccum = 0.0f;
        }
        
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
    g_state.lastFrameTime = Clock::now();
}

void EndFrame() {
    auto now = Clock::now();
    g_state.deltaTime = std::chrono::duration<float>(now - g_state.lastFrameTime).count();
    g_state.totalTime += g_state.deltaTime;
    
    g_state.frameCount++;
    g_state.fpsAccum += g_state.deltaTime;
    if (g_state.fpsAccum >= 1.0f) {
        g_state.fps = g_state.frameCount;
        g_state.frameCount = 0;
        g_state.fpsAccum = 0.0f;
    }
}

void Quit() {
    g_state.shouldQuit = true;
}

bool ShouldQuit() {
    return g_state.shouldQuit;
}

float Delta() {
    return g_state.deltaTime;
}

double Time() {
    return g_state.totalTime;
}

int FPS() {
    return g_state.fps;
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

template<typename... Args>
void ScopedConnection<Args...>::Disconnect() {
    if (id != 0) {
        id = 0;
    }
}

template<typename... Args>
bool ScopedConnection<Args...>::Connected() const {
    return id != 0;
}

template class ScopedConnection<int, int>;
template class ScopedConnection<>;
template class ScopedConnection<std::string_view>;

template<typename... Args>
ScopedConnection<Args...> Signal<Args...>::Connect(std::function<void(Args...)> slot) {
    uint64_t connId = ++nextId;
    slots.push_back({connId, std::move(slot)});
    return ScopedConnection<Args...>{connId};
}

template<typename... Args>
void Signal<Args...>::Emit(Args... args) {
    for (auto& s : slots) {
        if (s.second) s.second(args...);
    }
}

template<typename... Args>
void Signal<Args...>::Clear() {
    slots.clear();
}

template class Signal<int, int>;
template class Signal<>;
template class Signal<std::string_view>;

}
