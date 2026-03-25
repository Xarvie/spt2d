#if defined(__EMSCRIPTEN__) && !defined(__WXGAME__)

#include "../Platform.h"
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#include <iostream>

namespace spt3d {
namespace detail {

static IInputSystem* g_inputSystem = nullptr;

static EM_BOOL onMouseEvent(int eventType, const EmscriptenMouseEvent* e, void*) {
    if (!g_inputSystem) return EM_FALSE;
    MouseEvent me;
    me.x = static_cast<float>(e->targetX);
    me.y = static_cast<float>(e->targetY);
    me.timestamp = static_cast<float>(emscripten_get_now()) / 1000.0f;
    switch (eventType) {
        case EMSCRIPTEN_EVENT_MOUSEDOWN: 
            me.type = MouseEvent::Down; 
            me.button = e->button; 
            std::cout << "[WebInput] Mouse down at (" << me.x << ", " << me.y << ")" << std::endl;
            break;
        case EMSCRIPTEN_EVENT_MOUSEUP: 
            me.type = MouseEvent::Up; 
            me.button = e->button; 
            break;
        default: return EM_FALSE;
    }
    g_inputSystem->onMouse.emit(me);
    return EM_TRUE;
}

static EM_BOOL onWheelEvent(int, const EmscriptenWheelEvent* e, void*) {
    if (!g_inputSystem) return EM_FALSE;
    MouseEvent me;
    me.type = MouseEvent::Wheel;
    me.x = static_cast<float>(e->mouse.targetX);
    me.y = static_cast<float>(e->mouse.targetY);
    me.deltaX = static_cast<float>(e->deltaX);
    me.deltaY = static_cast<float>(e->deltaY);
    me.timestamp = static_cast<float>(emscripten_get_now()) / 1000.0f;
    g_inputSystem->onMouse.emit(me);
    return EM_TRUE;
}

static EM_BOOL onKeyEvent(int eventType, const EmscriptenKeyboardEvent* e, void*) {
    if (!g_inputSystem) return EM_FALSE;
    KeyEvent ke;
    ke.key = e->key;
    ke.code = e->code;
    ke.timestamp = static_cast<float>(emscripten_get_now()) / 1000.0f;
    ke.type = (eventType == EMSCRIPTEN_EVENT_KEYDOWN) ? KeyEvent::Down : KeyEvent::Up;
    g_inputSystem->onKey.emit(ke);
    return EM_TRUE;
}

static EM_BOOL onTouchEvent(int eventType, const EmscriptenTouchEvent* e, void*) {
    if (!g_inputSystem) return EM_FALSE;
    for (int i = 0; i < e->numTouches; ++i) {
        const auto* tp = &e->touches[i];
        if (!tp->isChanged) continue;
        TouchEvent te;
        te.id = tp->identifier;
        te.x = static_cast<float>(tp->targetX);
        te.y = static_cast<float>(tp->targetY);
        te.timestamp = static_cast<float>(emscripten_get_now()) / 1000.0f;
        switch (eventType) {
            case EMSCRIPTEN_EVENT_TOUCHSTART: te.type = TouchEvent::Begin; break;
            case EMSCRIPTEN_EVENT_TOUCHEND: case EMSCRIPTEN_EVENT_TOUCHCANCEL: te.type = TouchEvent::End; break;
            case EMSCRIPTEN_EVENT_TOUCHMOVE: te.type = TouchEvent::Move; break;
            default: continue;
        }
        g_inputSystem->onTouch.emit(te);
    }
    return EM_TRUE;
}

class WebWindowSystem : public IWindowSystem {
public:
    bool initialize() override {
        m_pixelRatio = static_cast<float>(emscripten_get_device_pixel_ratio());

        int w, h;
        emscripten_get_canvas_element_size("#canvas", &w, &h);
        m_width = w;
        m_height = h;

        std::cout << "[WebWindow] Canvas size: " << m_width << "x" << m_height << " (DPR=" << m_pixelRatio << ")" << std::endl;

        EmscriptenWebGLContextAttributes attrs;
        emscripten_webgl_init_context_attributes(&attrs);
        attrs.majorVersion = 2;
        EMSCRIPTEN_WEBGL_CONTEXT_HANDLE ctx = emscripten_webgl_create_context("#canvas", &attrs);
        if (ctx <= 0) {
            std::cerr << "[WebWindow] Failed to create WebGL context: " << ctx << std::endl;
            return false;
        }
        emscripten_webgl_make_context_current(ctx);
        m_context = ctx;

        std::cout << "[WebWindow] WebGL2 context created" << std::endl;
        return true;
    }

    void shutdown() override {}
    void setKeepScreenOn(bool) override {}
    void setScreenBrightness(float) override {}
    void* getGlContext() override { return reinterpret_cast<void*>(m_context); }
    void swapBuffers() override {}

    WindowInfo getWindowInfo() const override {
        WindowInfo info;
        info.windowWidth = m_width;
        info.windowHeight = m_height;
        info.screenWidth = m_width;
        info.screenHeight = m_height;
        info.pixelRatio = m_pixelRatio;
        return info;
    }

    SystemInfo getSystemInfo() const override {
        SystemInfo info;
        info.platform = "web";
        info.system = "browser";
        info.language = "en";
        info.benchmarkLevel = 100;
        return info;
    }

    void updateSize() {
        int newW, newH;
        emscripten_get_canvas_element_size("#canvas", &newW, &newH);

        if (newW != m_width || newH != m_height) {
            m_width = newW;
            m_height = newH;
            m_pixelRatio = static_cast<float>(emscripten_get_device_pixel_ratio());
            onWindowResize.emit(m_width, m_height);
        }
    }

private:
    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE m_context = 0;
    int m_width = 800, m_height = 600;
    float m_pixelRatio = 1.0f;
};

class WebInputSystem : public IInputSystem {
public:
    bool initialize() override {
        g_inputSystem = this;
        std::cout << "[WebInput] Registering callbacks..." << std::endl;

        EM_BOOL result;
        result = emscripten_set_mousedown_callback("#canvas", nullptr, EM_TRUE, onMouseEvent);
        std::cout << "[WebInput] mousedown callback result: " << result << std::endl;

        result = emscripten_set_mouseup_callback("#canvas", nullptr, EM_TRUE, onMouseEvent);
        result = emscripten_set_wheel_callback("#canvas", nullptr, EM_TRUE, onWheelEvent);
        result = emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, EM_TRUE, onKeyEvent);
        result = emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, EM_TRUE, onKeyEvent);
        result = emscripten_set_touchstart_callback("#canvas", nullptr, EM_TRUE, onTouchEvent);
        result = emscripten_set_touchend_callback("#canvas", nullptr, EM_TRUE, onTouchEvent);
        result = emscripten_set_touchmove_callback("#canvas", nullptr, EM_TRUE, onTouchEvent);
        result = emscripten_set_touchcancel_callback("#canvas", nullptr, EM_TRUE, onTouchEvent);

        std::cout << "[WebInput] Callbacks registered" << std::endl;
        return true;
    }
    void shutdown() override { g_inputSystem = nullptr; }
    void showKeyboard(const KeyboardConfig&) override {}
    void hideKeyboard() override {}
    void vibrateShort(const std::string&) override {}
    void vibrateLong() override {}
};

class WebNetworkSystem : public INetworkSystem {
public:
    bool initialize() override { return true; }
    void shutdown() override {}
    void sendHttpRequest(const HttpRequest&, std::function<void(const HttpResponse&)> cb) override {
        if (cb) {
            HttpResponse resp;
            resp.success = false;
            resp.statusCode = 501;
            resp.body = "";
            resp.errMsg = "Use fetch() in JavaScript";
            cb(resp);
        }
    }
    std::string getNetworkType() const override { return "unknown"; }
    void update(float) override {}
};

class WebStorageSystem : public IStorageSystem {
public:
    bool initialize() override { return true; }
    void shutdown() override {}
    void setItem(const std::string&, const std::string&) override {}
    std::string getItem(const std::string&) const override { return ""; }
    void removeItem(const std::string&) override {}
    void clearAll() override {}
};

class WebPlatformHub : public IPlatformHub {
public:
    bool initialize(const PlatformConfig&) override {
        m_windowSystem = std::make_unique<WebWindowSystem>();
        m_inputSystem = std::make_unique<WebInputSystem>();
        m_networkSystem = std::make_unique<WebNetworkSystem>();
        m_storageSystem = std::make_unique<WebStorageSystem>();
        if (!m_windowSystem->initialize()) return false;
        if (!m_inputSystem->initialize()) return false;
        if (!m_networkSystem->initialize()) return false;
        if (!m_storageSystem->initialize()) return false;
        std::cout << "[WebPlatformHub] Initialized" << std::endl;
        return true;
    }

    void runMainLoop(std::function<void(float)> updateFunc) override {
        m_updateFunc = std::move(updateFunc);
        m_lastTime = emscripten_get_now() / 1000.0;
        emscripten_set_main_loop_arg([](void* arg) {
            auto* self = static_cast<WebPlatformHub*>(arg);
            double now = emscripten_get_now() / 1000.0;
            float dt = std::min(static_cast<float>(now - self->m_lastTime), 0.1f);
            self->m_lastTime = now;

            self->m_windowSystem->updateSize();

            if (self->m_updateFunc) self->m_updateFunc(dt);
        }, this, 0, 1);
    }

    void shutdown() override { emscripten_cancel_main_loop(); }
    void exitApplication() override { shutdown(); }

    IWindowSystem* getWindowSystem() const override { return m_windowSystem.get(); }
    IInputSystem* getInputSystem() const override { return m_inputSystem.get(); }
    IAudioSystem* getAudioSystem() const override { return nullptr; }
    INetworkSystem* getNetworkSystem() const override { return m_networkSystem.get(); }
    IStorageSystem* getStorageSystem() const override { return m_storageSystem.get(); }

private:
    double m_lastTime = 0.0;
    std::function<void(float)> m_updateFunc;
    std::unique_ptr<WebWindowSystem> m_windowSystem;
    std::unique_ptr<IInputSystem> m_inputSystem;
    std::unique_ptr<INetworkSystem> m_networkSystem;
    std::unique_ptr<IStorageSystem> m_storageSystem;
};

std::unique_ptr<IPlatformHub> createPlatform_Web() {
    return std::make_unique<WebPlatformHub>();
}

} // namespace detail
} // namespace spt3d

#endif
