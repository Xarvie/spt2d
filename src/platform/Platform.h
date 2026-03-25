// spt3d/platform/Platform.h — Platform abstraction
// [THREAD: main] — Platform calls happen on the main/render thread.
#pragma once

#include "../Types.h"
#include "../core/Signal.h"

#include <memory>
#include <string>
#include <string_view>
#include <functional>

namespace spt3d {

struct PlatformConfig {
    int         width       = 800;
    int         height      = 600;
    std::string title       = "SptGame";
    bool        highDpi     = true;
    bool        resizable   = true;
    bool        vsync       = true;
    bool        isFullscreen = false;
    int         msaa        = 0;
    int         targetFps   = 60;
    std::string canvasId    = "#canvas";
};

struct SafeArea {
    int left = 0, top = 0, right = 0, bottom = 0;
    int width = 0, height = 0;
};

struct WindowInfo {
    int   screenWidth  = 0;
    int   screenHeight = 0;
    int   windowWidth  = 0;
    int   windowHeight = 0;
    int   renderWidth  = 0;
    int   renderHeight = 0;
    float pixelRatio   = 1.0f;
    bool  focused      = true;
    bool  fullscreen   = false;
    SafeArea safeArea;
};

struct SystemInfo {
    std::string platform;
    std::string system;
    std::string language;
    int         benchmarkLevel = 0;
};

struct KeyboardConfig {
    bool multiline = false;
    std::string placeholder;
    std::string defaultValue;
    std::string confirmType = "done";
    int maxLength = 0;
    bool isMultiple = false;
};

class IWindowSystem {
public:
    Signal<int, int> onWindowResize;
    Signal<float> onSafeAreaChanged;
    
    virtual ~IWindowSystem() = default;
    virtual bool initialize() { return false; }
    virtual void shutdown() {}
    virtual WindowInfo getWindowInfo() const = 0;
    virtual SystemInfo getSystemInfo() const = 0;
    virtual void       swapBuffers() = 0;
    virtual void       setKeepScreenOn(bool keep) = 0;
    virtual void       setScreenBrightness(float brightness) = 0;
    virtual void*      getGlContext() = 0;
};

class IInputSystem {
public:
    Signal<const KeyEvent&>   onKey;
    Signal<const MouseEvent&> onMouse;
    Signal<const TouchEvent&> onTouch;
    
    virtual ~IInputSystem() = default;
    virtual bool initialize() = 0;
    virtual void shutdown() = 0;
    virtual void showKeyboard(const KeyboardConfig& config) = 0;
    virtual void hideKeyboard() = 0;
    virtual void vibrateShort(const std::string& type) = 0;
    virtual void vibrateLong() = 0;
};

class IAudioSystem {
public:
    virtual ~IAudioSystem() = default;
};

class INetworkSystem {
public:
    virtual ~INetworkSystem() = default;
    virtual bool initialize() = 0;
    virtual void shutdown() = 0;
    virtual void sendHttpRequest(const HttpRequest& request, std::function<void(const HttpResponse&)> callback) = 0;
    virtual std::string getNetworkType() const = 0;
    virtual void update(float dt) = 0;
};

class IStorageSystem {
public:
    virtual ~IStorageSystem() = default;
    virtual bool initialize() = 0;
    virtual void shutdown() = 0;
    virtual void setItem(const std::string& key, const std::string& value) = 0;
    virtual std::string getItem(const std::string& key) const = 0;
    virtual void removeItem(const std::string& key) = 0;
    virtual void clearAll() = 0;
};

class IPlatformHub {
public:
    Signal<> onAppShow;
    Signal<> onAppHide;
    Signal<std::string_view> onError;
    Signal<int> onMemoryWarning;
    
    virtual ~IPlatformHub() = default;
    virtual bool initialize(const PlatformConfig& config) = 0;
    virtual void shutdown() = 0;
    virtual void runMainLoop(std::function<void(float)> updateFunc) = 0;
    virtual void pollEvents() {}
    virtual IWindowSystem* getWindowSystem() const = 0;
    virtual IInputSystem*  getInputSystem()  const = 0;
    virtual IAudioSystem*   getAudioSystem() const = 0;
    virtual INetworkSystem* getNetworkSystem() const = 0;
    virtual IStorageSystem* getStorageSystem() const = 0;
    virtual void exitApplication() = 0;
};

// Public API - automatically selects the correct platform
std::unique_ptr<IPlatformHub> createPlatform();

// Private platform-specific implementations (do not call directly)
namespace detail {
    std::unique_ptr<IPlatformHub> createPlatform_Sdl3();
    std::unique_ptr<IPlatformHub> createPlatform_Wx();
    std::unique_ptr<IPlatformHub> createPlatform_Web();
}

} // namespace spt3d
