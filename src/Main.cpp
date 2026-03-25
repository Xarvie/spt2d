#include "Spt3D.h"
#include "core/ThreadModel.h"
#include "vfs/VirtualFileSystem.h"
#include "vfs/providers/NativeFileSystem.h"
#include "resource/ResourceManager.h"

#include <iostream>
#include <exception>
#include <memory>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

namespace spt3d {

static std::unique_ptr<IPlatformHub> g_platform;
static std::unique_ptr<ThreadModel> g_threadModel;

IPlatformHub* GetPlatform() { return g_platform.get(); }

ResourceManager& GetResourceManager() {
    return ResourceManager::Instance();
}

class GameLogicImpl : public IGameLogic {
public:
    bool onInit() override {
        std::cout << "[Game] Initializing..." << std::endl;

        auto windowInfo = g_platform->getWindowSystem()->getWindowInfo();
        std::cout << "[Game] Window: " << windowInfo.screenWidth << "x" << windowInfo.screenHeight << std::endl;

        m_windowWidth = windowInfo.screenWidth;
        m_windowHeight = windowInfo.screenHeight;

#ifndef __WXGAME__
        VirtualFileSystem::Instance().mount("file", 
            std::make_unique<NativeFileSystemProvider>("./"));
#endif

        m_running = true;
        std::cout << "[Game] Initialized successfully" << std::endl;
        return true;
    }

    void onUpdate(float dt, const InputFrame& input) override {
        (void)dt;
        
        for (const auto& e : input.touchEvents()) {
            std::cout << "[Game] Touch: id=" << e.id << " x=" << e.x << " y=" << e.y << std::endl;
        }
        for (const auto& e : input.keyEvents()) {
            std::cout << "[Game] Key: " << e.key << " (" << (e.type == KeyEvent::Down ? "Down" : "Up") << ")" << std::endl;
            if (e.key == "Escape" || e.key == "q") {
                m_running = false;
            }
        }
        for (const auto& e : input.mouseEvents()) {
            std::cout << "[Game] Mouse: x=" << e.x << " y=" << e.y << std::endl;
        }
        
        ResourceManager::Instance().update();
        VirtualFileSystem::Instance().processCompleted();
    }

    void onRender(GameWork& work) override {
        work.reset();
    }

    void onShutdown() override {
        std::cout << "[Game] Shutting down..." << std::endl;
    }

    bool isRunning() const override {
        return m_running;
    }

private:
    bool m_running = false;
    int m_windowWidth = 1280;
    int m_windowHeight = 720;
};

static GameLogicImpl* g_gameLogic = nullptr;
static ScopedConnection<const KeyEvent&> g_keyConn;
static ScopedConnection<const MouseEvent&> g_mouseConn;
static ScopedConnection<const TouchEvent&> g_touchConn;

static void connectInputToThreadModel() {
    auto* input = g_platform->getInputSystem();
    if (!input || !g_threadModel) return;
    
    g_keyConn = input->onKey.connect([](const KeyEvent& e) {
        if (g_threadModel) g_threadModel->pushKeyEvent(e);
    });
    
    g_mouseConn = input->onMouse.connect([](const MouseEvent& e) {
        if (g_threadModel) g_threadModel->pushMouseEvent(e);
    });
    
    g_touchConn = input->onTouch.connect([](const TouchEvent& e) {
        if (g_threadModel) g_threadModel->pushTouchEvent(e);
    });
}

static void mainLoopFrame(float dt) {
    (void)dt;

    if (!g_threadModel || !g_threadModel->isRunning()) {
        g_platform->exitApplication();
        return;
    }

    g_threadModel->onFrameBegin();
    
    const GameWork* work = g_threadModel->getRenderWork();
    if (work) {
        // TODO: Execute render commands via Executor
        // executor.execute(*work, gpu);
    }
    
    g_threadModel->onFrameEnd();
    
    g_platform->getWindowSystem()->swapBuffers();
}

} // namespace spt3d

int main(int argc, char* argv[]) {
    using namespace spt3d;

    g_platform = createPlatform();

    if (!g_platform) {
        std::cerr << "Failed to create platform" << std::endl;
        return -1;
    }

    PlatformConfig config;
    config.width = 1280;
    config.height = 720;
    config.title = "spt3d";

    if (!g_platform->initialize(config)) {
        std::cerr << "Platform initialization failed" << std::endl;
        return -1;
    }

    ThreadConfig threadConfig;
#if defined(__EMSCRIPTEN__)
    threadConfig.multithreaded = false;
#elif defined(__WXGAME__)
    threadConfig.multithreaded = false;
#else
    threadConfig.multithreaded = true;
    threadConfig.logicHz = 30;
    threadConfig.renderHz = 60;
#endif

    g_threadModel = ThreadModel::create(threadConfig);

    auto gameLogicPtr = std::make_unique<GameLogicImpl>();
    g_gameLogic = gameLogicPtr.get();

    if (!g_threadModel->initialize(threadConfig, std::move(gameLogicPtr))) {
        std::cerr << "ThreadModel initialization failed" << std::endl;
        return -1;
    }

    connectInputToThreadModel();

#ifdef __EMSCRIPTEN__
    try {
        emscripten_set_main_loop_arg([](void*) {
            mainLoopFrame(0.016f);
        }, nullptr, 0, 1);
    } catch (const std::exception& e) {
        std::string what = e.what() ? e.what() : "";
        if (what.find("unwind") != std::string::npos) {
            std::cout << "[Game] Main loop started" << std::endl;
        } else {
            std::cerr << "[Game] Exception: " << what << std::endl;
        }
    } catch (...) {
        std::cout << "[Game] Main loop started" << std::endl;
    }
#else
    g_platform->runMainLoop(mainLoopFrame);
    g_threadModel->shutdown();
    g_platform->shutdown();
#endif

    return 0;
}
