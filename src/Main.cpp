#include "platform/Platform.h"
#include "graphics/Graphics.h"
#include "graphics/RenderCommandExecutor.h"
#include "core/ThreadModel.h"
#include "core/Signal.h"

#include <iostream>
#include <exception>
#include <memory>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

namespace spt {

static IPlatformHub* g_platform = nullptr;
static std::unique_ptr<Graphics> g_graphics;
static std::unique_ptr<ThreadModel> g_threadModel;
static RenderCommandExecutor g_renderExecutor;

IPlatformHub* GetPlatform() { return g_platform; }

class GameLogicImpl : public IGameLogic {
public:
    bool onInit() override {
        std::cout << "[Game] Initializing..." << std::endl;

        auto windowInfo = GetPlatform()->getWindowSystem()->getWindowInfo();
        std::cout << "[Game] Window: " << windowInfo.windowWidth << "x" << windowInfo.windowHeight << std::endl;

        g_graphics = std::make_unique<Graphics>();
        if (!g_graphics->initialize()) {
            std::cerr << "[Game] Failed to initialize graphics" << std::endl;
            return false;
        }

        m_windowWidth = windowInfo.windowWidth;
        m_windowHeight = windowInfo.windowHeight;

        m_resizeConn = GetPlatform()->getWindowSystem()->onWindowResize.connect([this](int w, int h) {
            std::cout << "[Game] Window resized: " << w << "x" << h << std::endl;
            m_windowWidth = w;
            m_windowHeight = h;
            m_viewportDirty = true;
        });

        if (auto* input = GetPlatform()->getInputSystem()) {
            m_keyConn = input->onKey.connect([](const KeyEvent& e) {
                if (e.type == KeyEvent::Down) {
                    std::cout << "[Game] Key pressed: " << e.key << std::endl;
                    if (e.key == "Escape" || e.key == "q") {
                        GetPlatform()->exitApplication();
                    }
                }
            });

            m_touchConn = input->onTouch.connect([](const TouchEvent& e) {
                const char* typeName = e.type == TouchEvent::Begin ? "Begin" :
                                       e.type == TouchEvent::Move ? "Move" :
                                       e.type == TouchEvent::End ? "End" : "Cancel";
                std::cout << "[Game] Touch " << typeName << ": id=" << e.id 
                          << " x=" << e.x << " y=" << e.y << std::endl;
            });

            m_mouseConn = input->onMouse.connect([](const MouseEvent& e) {
                const char* typeName = e.type == MouseEvent::Down ? "Down" :
                                       e.type == MouseEvent::Up ? "Up" :
                                       e.type == MouseEvent::Move ? "Move" : "Wheel";
                std::cout << "[Game] Mouse " << typeName << ": x=" << e.x << " y=" << e.y << std::endl;
            });
        }

        m_running = true;
        std::cout << "[Game] Initialized successfully" << std::endl;
        return true;
    }

    void onUpdate(float dt, const InputFrame& input) override {
        (void)dt;
        (void)input;
    }

    void onRender(GameWork& work) override {
        if (m_viewportDirty) {
            buildViewportCommand(work, 0, 0, m_windowWidth, m_windowHeight);
            m_viewportDirty = false;
        }

        buildClearCommand(work, GL_COLOR_BUFFER_BIT, 0.1f, 0.1f, 0.15f, 1.0f);

        if (g_graphics) {
            g_graphics->recordTriangle(
                work,
                -0.5f, -0.5f,  1.0f, 0.0f, 0.0f,
                 0.5f, -0.5f,  0.0f, 1.0f, 0.0f,
                 0.0f,  0.5f,  0.0f, 0.0f, 1.0f
            );
        }
    }

    void onShutdown() override {
        std::cout << "[Game] Shutting down..." << std::endl;
        m_resizeConn.disconnect();
        m_keyConn.disconnect();
        m_touchConn.disconnect();
        m_mouseConn.disconnect();
    }

    bool isRunning() const override {
        return m_running;
    }

private:
    bool m_running = false;
    ScopedConnection<int, int> m_resizeConn;
    ScopedConnection<const KeyEvent&> m_keyConn;
    ScopedConnection<const TouchEvent&> m_touchConn;
    ScopedConnection<const MouseEvent&> m_mouseConn;
    int m_windowWidth = 1280;
    int m_windowHeight = 720;
    bool m_viewportDirty = true;
};

static GameLogicImpl* g_gameLogic = nullptr;

static void mainLoopFrame(float dt) {
    (void)dt;

    if (!g_threadModel || !g_threadModel->isRunning()) {
        GetPlatform()->exitApplication();
        return;
    }

    g_threadModel->onFrameBegin();

    const GameWork* work = g_threadModel->getRenderWork();
    if (work) {
        g_renderExecutor.execute(*work);
    }

    g_threadModel->onFrameEnd();
}

} // namespace spt

int main(int argc, char* argv[]) {
    using namespace spt;

#ifdef __WXGAME__
    auto platform = createPlatformWx();
#elif defined(__EMSCRIPTEN__)
    auto platform = createPlatformWeb();
#else
    auto platform = createPlatformSdl3();
#endif

    if (!platform) {
        std::cerr << "Failed to create platform" << std::endl;
        return -1;
    }



    PlatformConfig config;
    config.width = 1280;
    config.height = 720;
    config.title = "spt-2d";
    config.targetFps = 60;

    if (!platform->initialize(config)) {
        std::cerr << "Platform initialization failed" << std::endl;
        return -1;
    }

    g_platform = platform.get();

    ThreadConfig threadConfig;
#ifdef __EMSCRIPTEN__
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

#ifdef __EMSCRIPTEN__
    try {
        platform->runMainLoop(mainLoopFrame);
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
    platform->runMainLoop(mainLoopFrame);
    g_threadModel->shutdown();
    g_graphics.reset();
    platform->shutdown();
#endif

    return 0;
}
