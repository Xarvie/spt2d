#ifdef __WXGAME__

#include "../Platform.h"
#include "../../core/CallbackManager.h"
#include <emscripten/emscripten.h>
#include <iostream>
#include <map>

namespace spt {

static IInputSystem* g_inputSystem = nullptr;
static INetworkSystem* g_networkSystem = nullptr;
static IWindowSystem* g_windowSystem = nullptr;
static CallbackManager<HttpResponse>* g_httpCallbackManager = nullptr;

extern "C" {

EMSCRIPTEN_KEEPALIVE void WxBridge_OnTouch(int type, int id, float x, float y, float ts) {
    if (g_inputSystem) {
        TouchEvent te{ static_cast<TouchEvent::Type>(type), id, x, y, ts };
        g_inputSystem->onTouch.emit(te);
    }
}

EMSCRIPTEN_KEEPALIVE void WxBridge_OnKey(int type, const char* key, const char* code, float ts) {
    if (g_inputSystem) {
        KeyEvent ke{ static_cast<KeyEvent::Type>(type), key ? key : "", code ? code : "", ts };
        g_inputSystem->onKey.emit(ke);
    }
}

EMSCRIPTEN_KEEPALIVE void WxBridge_OnMouse(int type, float x, float y, int btn, float dx, float dy, float ts) {
    if (g_inputSystem) {
        MouseEvent me{ static_cast<MouseEvent::Type>(type), x, y, btn, dx, dy, ts };
        g_inputSystem->onMouse.emit(me);
    }
}

EMSCRIPTEN_KEEPALIVE void WxBridge_OnResize(int w, int h) {
    if (g_windowSystem) g_windowSystem->onWindowResize.emit(w, h);
}

EMSCRIPTEN_KEEPALIVE void WxBridge_OnSafeAreaChange(int l, int t, int r, int b) {
    if (g_windowSystem) g_windowSystem->onSafeAreaChanged.emit(static_cast<float>(r - l));
}

EMSCRIPTEN_KEEPALIVE void WxBridge_OnHttpResponse(int cbId, int success, int status, const char* data, const char* errMsg) {
    if (g_httpCallbackManager) {
        HttpResponse resp{ success != 0, status, data ? data : "", errMsg ? errMsg : "" };
        g_httpCallbackManager->invokeAndRemove(cbId, resp);
    }
}

EMSCRIPTEN_KEEPALIVE void WxBridge_OnAppShow() {
    if (auto* hub = GetPlatform()) hub->onAppShow.emit();
}

EMSCRIPTEN_KEEPALIVE void WxBridge_OnAppHide() {
    if (auto* hub = GetPlatform()) hub->onAppHide.emit();
}

EMSCRIPTEN_KEEPALIVE void WxBridge_OnError(const char* msg) {
    if (auto* hub = GetPlatform()) hub->onError.emit(msg ? msg : "");
}

EMSCRIPTEN_KEEPALIVE void WxBridge_OnMemoryWarning(int level) {
    if (auto* hub = GetPlatform()) hub->onMemoryWarning.emit(level);
}

}

class WxWindowSystem : public IWindowSystem {
public:
    bool initialize() override {
        std::cout << "[WxWindow] Initialized" << std::endl;
        return true;
    }
    void shutdown() override {}
    void setKeepScreenOn(bool keep) override {
        EM_ASM({ if (typeof wx !== 'undefined') wx.setKeepScreenOn({ keepScreenOn: $0 }); }, keep);
    }
    void setScreenBrightness(float v) override {
        EM_ASM({ if (typeof wx !== 'undefined') wx.setScreenBrightness({ value: Math.max(0, Math.min(1, $0)) }); }, v);
    }
    void* getGlContext() override { return nullptr; }
    void swapBuffers() override {}

    WindowInfo getWindowInfo() const override {
        WindowInfo info;
        EM_ASM({
            if (typeof wx !== 'undefined') {
                try {
                    var si = wx.getSystemInfoSync();
                    setValue($0, si.windowWidth || 0, 'i32');
                    setValue($1, si.windowHeight || 0, 'i32');
                    setValue($2, si.pixelRatio || 1.0, 'float');
                } catch(e) {}
            }
        }, &info.windowWidth, &info.windowHeight, &info.pixelRatio);
        info.screenWidth = info.windowWidth;
        info.screenHeight = info.windowHeight;
        return info;
    }

    SystemInfo getSystemInfo() const override {
        SystemInfo info;
        info.platform = "wx";
        return info;
    }
};

class WxInputSystem : public IInputSystem {
public:
    bool initialize() override {
        g_inputSystem = this;
        std::cout << "[WxInput] Initialized" << std::endl;
        return true;
    }
    void shutdown() override { g_inputSystem = nullptr; }

    void showKeyboard(const KeyboardConfig& cfg) override {
        EM_ASM({
            if (typeof wx !== 'undefined') wx.showKeyboard({
                defaultValue: UTF8ToString($0),
                maxLength: $1,
                multiple: $2,
                confirmType: UTF8ToString($3)
            });
        }, cfg.defaultValue.c_str(), cfg.maxLength, cfg.isMultiple, cfg.confirmType.c_str());
    }
    void hideKeyboard() override { EM_ASM({ if (typeof wx !== 'undefined') wx.hideKeyboard(); }); }
    void vibrateShort(const std::string& type) override {
        EM_ASM({ if (typeof wx !== 'undefined') wx.vibrateShort({ type: UTF8ToString($0) }); }, type.c_str());
    }
    void vibrateLong() override { EM_ASM({ if (typeof wx !== 'undefined') wx.vibrateLong(); }); }
};

class WxNetworkSystem : public INetworkSystem {
public:
    bool initialize() override {
        g_networkSystem = this;
        g_httpCallbackManager = &m_callbackManager;
        std::cout << "[WxNetwork] Initialized" << std::endl;
        return true;
    }
    void shutdown() override { g_networkSystem = nullptr; g_httpCallbackManager = nullptr; }

    void sendHttpRequest(const HttpRequest& req, std::function<void(const HttpResponse&)> cb) override {
        int id = m_callbackManager.registerCallback(std::move(cb), 10.0f);
        std::string headersJson = "{";
        bool first = true;
        for (const auto& h : req.headers) {
            if (!first) headersJson += ",";
            headersJson += "\"" + h.first + "\":\"" + h.second + "\"";
            first = false;
        }
        headersJson += "}";
        EM_ASM({
            if (typeof wx === 'undefined') return;
            var cbId = $0;
            var url = UTF8ToString($1);
            var method = UTF8ToString($2) || 'GET';
            var data = UTF8ToString($3);
            var headers = JSON.parse(UTF8ToString($4) || '{}');
            wx.request({
                url: url, method: method, data: data, header: headers,
                success: function(res) {
                    var d = typeof res.data === 'string' ? res.data : JSON.stringify(res.data);
                    var dLen = lengthBytesUTF8(d) + 1, dPtr = _malloc(dLen);
                    stringToUTF8(d, dPtr, dLen);
                    var ePtr = _malloc(1); stringToUTF8("", ePtr, 1);
                    _WxBridge_OnHttpResponse(cbId, 1, res.statusCode, dPtr, ePtr);
                    _free(dPtr); _free(ePtr);
                },
                fail: function(err) {
                    var msg = err.errMsg || 'request failed';
                    var mLen = lengthBytesUTF8(msg) + 1, mPtr = _malloc(mLen);
                    stringToUTF8(msg, mPtr, mLen);
                    var dPtr = _malloc(1); stringToUTF8("", dPtr, 1);
                    _WxBridge_OnHttpResponse(cbId, 0, 0, dPtr, mPtr);
                    _free(dPtr); _free(mPtr);
                }
            });
        }, id, req.url.c_str(), req.method.c_str(), req.data.c_str(), headersJson.c_str());
    }

    std::string getNetworkType() const override {
        char buf[64] = {0};
        EM_ASM({
            if (typeof wx !== 'undefined') {
                try { var t = wx.getNetworkType().networkType || 'unknown'; stringToUTF8(t, $0, 64); } catch(e) {}
            }
        }, buf);
        return std::string(buf);
    }

    void update(float dt) override {
        HttpResponse timeoutResp{ false, 408, "", "Timeout" };
        m_callbackManager.tick(dt, timeoutResp);
    }

private:
    CallbackManager<HttpResponse> m_callbackManager;
};

class WxStorageSystem : public IStorageSystem {
public:
    bool initialize() override { return true; }
    void shutdown() override {}
    void setItem(const std::string& key, const std::string& value) override {
        EM_ASM({ if (typeof wx !== 'undefined') wx.setStorageSync(UTF8ToString($0), UTF8ToString($1)); }, key.c_str(), value.c_str());
    }
    std::string getItem(const std::string& key) const override {
        char buf[4096] = {0};
        EM_ASM({
            if (typeof wx !== 'undefined') {
                try { var v = wx.getStorageSync(UTF8ToString($0)) || ""; stringToUTF8(String(v), $1, 4096); } catch(e) {}
            }
        }, key.c_str(), buf);
        return std::string(buf);
    }
    void removeItem(const std::string& key) override {
        EM_ASM({ if (typeof wx !== 'undefined') wx.removeStorageSync(UTF8ToString($0)); }, key.c_str());
    }
    void clearAll() override { EM_ASM({ if (typeof wx !== 'undefined') wx.clearStorageSync(); }); }
};

class WxPlatformHub : public IPlatformHub {
public:
    bool initialize(const PlatformConfig&) override {
        m_windowSystem = std::make_unique<WxWindowSystem>();
        m_inputSystem = std::make_unique<WxInputSystem>();
        m_networkSystem = std::make_unique<WxNetworkSystem>();
        m_storageSystem = std::make_unique<WxStorageSystem>();
        if (!m_windowSystem->initialize()) return false;
        if (!m_inputSystem->initialize()) return false;
        if (!m_networkSystem->initialize()) return false;
        if (!m_storageSystem->initialize()) return false;
        g_windowSystem = m_windowSystem.get();
        std::cout << "[WxPlatformHub] Initialized" << std::endl;
        return true;
    }

    void runMainLoop(std::function<void(float)> updateFunc) override {
        m_updateFunc = std::move(updateFunc);
        m_lastTime = emscripten_get_now() / 1000.0;
        emscripten_set_main_loop_arg([](void* arg) {
            auto* self = static_cast<WxPlatformHub*>(arg);
            double now = emscripten_get_now() / 1000.0;
            float dt = std::min(static_cast<float>(now - self->m_lastTime), 0.1f);
            self->m_lastTime = now;
            self->m_networkSystem->update(dt);
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
    std::unique_ptr<IWindowSystem> m_windowSystem;
    std::unique_ptr<IInputSystem> m_inputSystem;
    std::unique_ptr<WxNetworkSystem> m_networkSystem;
    std::unique_ptr<IStorageSystem> m_storageSystem;
};

std::unique_ptr<IPlatformHub> createPlatformWx() {
    return std::make_unique<WxPlatformHub>();
}

} // namespace spt

#endif
