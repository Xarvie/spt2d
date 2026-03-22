#pragma once

#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include <functional>
#include <memory>
#include "../core/Signal.h"

namespace spt {

// ============================================================================
// Platform Types
// ============================================================================

struct SafeArea {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
    int width = 0;
    int height = 0;
};

struct WindowInfo {
    int windowWidth = 0;
    int windowHeight = 0;
    int screenWidth = 0;
    int screenHeight = 0;
    float pixelRatio = 1.0f;
    SafeArea safeArea;
};

struct SystemInfo {
    std::string brand;
    std::string model;
    std::string system;
    std::string platform;
    std::string language;
    int benchmarkLevel = 50;
};

struct TouchEvent {
    enum Type { Begin = 0, Move = 1, End = 2, Cancel = 3 };
    Type type = Begin;
    int id = 0;
    float x = 0.0f;
    float y = 0.0f;
    float timestamp = 0.0f;
};

struct KeyEvent {
    enum Type { Down = 0, Up = 1 };
    Type type = Down;
    std::string key;
    std::string code;
    float timestamp = 0.0f;
};

struct MouseEvent {
    enum Type { Down = 0, Up = 1, Move = 2, Wheel = 3 };
    Type type = Down;
    float x = 0.0f;
    float y = 0.0f;
    int button = 0;
    float deltaX = 0.0f;
    float deltaY = 0.0f;
    float timestamp = 0.0f;
};

struct KeyboardConfig {
    std::string defaultValue;
    int maxLength = 100;
    bool isMultiple = false;
    bool confirmHold = false;
    std::string confirmType = "done";
};

struct HttpResponse {
    bool success = false;
    int statusCode = 0;
    std::string data;
    std::string errMsg;
    std::map<std::string, std::string> headers;
};

struct HttpRequest {
    std::string url;
    std::string method = "GET";
    std::string data;
    std::map<std::string, std::string> headers;
    int timeout = 30000;
    std::string responseType = "text";
};

struct PlatformConfig {
    int width = 0;
    int height = 0;
    std::string title = "Engine";
    int targetFps = 60;
    std::string orientation = "portrait";
    bool isFullscreen = false;
};

using AudioHandle = uint32_t;

// ============================================================================
// Subsystem Interfaces
// ============================================================================

class ISubsystem {
public:
    virtual ~ISubsystem() = default;
    [[nodiscard]] virtual bool initialize() = 0;
    virtual void update(float dt) { (void)dt; }
    virtual void shutdown() = 0;
};

class IWindowSystem : public ISubsystem {
public:
    virtual void setKeepScreenOn(bool keep) = 0;
    virtual void setScreenBrightness(float brightness) = 0;
    virtual void* getGlContext() = 0;
    virtual void swapBuffers() = 0;
    [[nodiscard]] virtual WindowInfo getWindowInfo() const = 0;
    [[nodiscard]] virtual SystemInfo getSystemInfo() const = 0;

    Signal<int, int> onWindowResize;
    Signal<float> onSafeAreaChanged;
};

class IInputSystem : public ISubsystem {
public:
    virtual void showKeyboard(const KeyboardConfig& config) = 0;
    virtual void hideKeyboard() = 0;
    virtual void vibrateShort(const std::string& type = "medium") = 0;
    virtual void vibrateLong() = 0;

    Signal<const TouchEvent&> onTouch;
    Signal<const KeyEvent&> onKey;
    Signal<const MouseEvent&> onMouse;
    Signal<const std::string&> onKeyboardInput;
    Signal<const std::string&> onKeyboardConfirm;
};

class IAudioSystem : public ISubsystem {
public:
    virtual AudioHandle createAudio(const std::string& path) = 0;
    virtual void playAudio(AudioHandle handle) = 0;
    virtual void pauseAudio(AudioHandle handle) = 0;
    virtual void stopAudio(AudioHandle handle) = 0;
    virtual void destroyAudio(AudioHandle handle) = 0;
    virtual void setVolume(AudioHandle handle, float volume) = 0;
    virtual void setLoop(AudioHandle handle, bool isLoop) = 0;

    Signal<AudioHandle> onAudioEnded;
};

class INetworkSystem : public ISubsystem {
public:
    virtual void sendHttpRequest(const HttpRequest& request, std::function<void(const HttpResponse&)> callback) = 0;
    [[nodiscard]] virtual std::string getNetworkType() const = 0;

    Signal<bool, const std::string&> onNetworkStatusChanged;
};

class IStorageSystem : public ISubsystem {
public:
    virtual void setItem(const std::string& key, const std::string& value) = 0;
    [[nodiscard]] virtual std::string getItem(const std::string& key) const = 0;
    virtual void removeItem(const std::string& key) = 0;
    virtual void clearAll() = 0;
};

// ============================================================================
// Platform Hub Interface
// ============================================================================

class IPlatformHub {
public:
    virtual ~IPlatformHub() = default;

    [[nodiscard]] virtual bool initialize(const PlatformConfig& config) = 0;
    virtual void runMainLoop(std::function<void(float)> gameUpdateFunc) = 0;
    virtual void shutdown() = 0;
    virtual void exitApplication() = 0;

    Signal<> onAppShow;
    Signal<> onAppHide;
    Signal<const std::string&> onError;
    Signal<int> onMemoryWarning;

    [[nodiscard]] virtual IWindowSystem* getWindowSystem() const = 0;
    [[nodiscard]] virtual IInputSystem* getInputSystem() const = 0;
    [[nodiscard]] virtual IAudioSystem* getAudioSystem() const = 0;
    [[nodiscard]] virtual INetworkSystem* getNetworkSystem() const = 0;
    [[nodiscard]] virtual IStorageSystem* getStorageSystem() const = 0;
};

// ============================================================================
// Factory Functions
// ============================================================================

std::unique_ptr<IPlatformHub> createPlatformSdl3();
std::unique_ptr<IPlatformHub> createPlatformWeb();
std::unique_ptr<IPlatformHub> createPlatformWx();

IPlatformHub* GetPlatform();

} // namespace spt
