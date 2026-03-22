#include "platform/Platform.h"
#include "graphics/Graphics.h"
#include <iostream>
#include <exception>
#include <memory>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

namespace spt {

static IPlatformHub* g_platform = nullptr;
static std::unique_ptr<Graphics> g_graphics;
static std::function<void(float)> g_updateFunc;

IPlatformHub* GetPlatform() { return g_platform; }

class GameLogic {
public:
    void initialize() {
        std::cout << "[Game] Initializing..." << std::endl;

        auto windowInfo = GetPlatform()->getWindowSystem()->getWindowInfo();
        std::cout << "[Game] Window: " << windowInfo.windowWidth << "x" << windowInfo.windowHeight << std::endl;

        g_graphics = std::make_unique<Graphics>();
        if (!g_graphics->initialize()) {
            std::cerr << "[Game] Failed to initialize graphics" << std::endl;
            return;
        }

        g_graphics->setViewport(0, 0, windowInfo.windowWidth, windowInfo.windowHeight);

        m_resizeSlotId = GetPlatform()->getWindowSystem()->onWindowResize.connect([this](int w, int h) {
            std::cout << "[Game] Window resized: " << w << "x" << h << std::endl;
            g_graphics->setViewport(0, 0, w, h);
        });

        if (auto* input = GetPlatform()->getInputSystem()) {
            m_keySlotId = input->onKey.connect([this](const KeyEvent& e) {
                if (e.type == KeyEvent::Down) {
                    std::cout << "[Game] Key pressed: " << e.key << std::endl;
                    if (e.key == "Escape" || e.key == "q") GetPlatform()->exitApplication();
                }
            });

            m_touchSlotId = input->onTouch.connect([this](const TouchEvent& e) {
                const char* typeName = e.type == TouchEvent::Begin ? "Begin" :
                                       e.type == TouchEvent::Move ? "Move" :
                                       e.type == TouchEvent::End ? "End" : "Cancel";
                std::cout << "[Game] Touch " << typeName << ": id=" << e.id 
                          << " x=" << e.x << " y=" << e.y << std::endl;
            });
        }

        std::cout << "[Game] Initialized successfully" << std::endl;
    }

    void update(float dt) {
        (void)dt;
    }

    void render() {
        g_graphics->clear(0.1f, 0.1f, 0.15f, 1.0f);

        g_graphics->drawTriangle(
            -0.5f, -0.5f,  1.0f, 0.0f, 0.0f,
             0.5f, -0.5f,  0.0f, 1.0f, 0.0f,
             0.0f,  0.5f,  0.0f, 0.0f, 1.0f
        );

        g_graphics->flush();
    }

    void shutdown() {
        std::cout << "[Game] Shutting down..." << std::endl;
        if (auto* window = GetPlatform()->getWindowSystem()) {
            window->onWindowResize.disconnect(m_resizeSlotId);
        }
        if (auto* input = GetPlatform()->getInputSystem()) {
            input->onKey.disconnect(m_keySlotId);
            input->onTouch.disconnect(m_touchSlotId);
        }
        g_graphics.reset();
    }

private:
    uint32_t m_resizeSlotId = 0, m_keySlotId = 0, m_touchSlotId = 0;
};

static GameLogic* g_game = nullptr;

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
    static GameLogic game;
    g_game = &game;
    game.initialize();

#ifdef __EMSCRIPTEN__
    try {
        platform->runMainLoop([](float dt) { g_game->update(dt); g_game->render(); });
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
    platform->runMainLoop([](float dt) { g_game->update(dt); g_game->render(); });
    game.shutdown();
    platform->shutdown();
#endif

    return 0;
}
