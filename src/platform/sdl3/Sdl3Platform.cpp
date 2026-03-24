#if !defined(__EMSCRIPTEN__) && !defined(__WXGAME__)

#include "../Platform.h"
#include "../../core/CallbackManager.h"
#include "../../glad/glad.h"
#include <SDL3/SDL.h>
#include <chrono>
#include <iostream>
#include <thread>
#include <mutex>
#include <queue>
#include <atomic>
#include <condition_variable>
#include <map>

namespace spt {

// ============================================================================
// SDL3 Window System
// ============================================================================

class Sdl3WindowSystem : public IWindowSystem {
public:
    ~Sdl3WindowSystem() override { shutdown(); }

    bool initialize() override { return false; }

    bool initialize(const PlatformConfig& config) {
#if defined(__APPLE__)
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#else
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
#endif
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

        int width = config.width > 0 ? config.width : 800;
        int height = config.height > 0 ? config.height : 600;

        SDL_WindowFlags winFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
        if (config.isFullscreen) {
            winFlags = static_cast<SDL_WindowFlags>(winFlags | SDL_WINDOW_FULLSCREEN);
        }

        m_window = SDL_CreateWindow(config.title.c_str(), width, height, winFlags);
        if (!m_window) {
            std::cerr << "[Sdl3Window] Failed to create window: " << SDL_GetError() << std::endl;
            return false;
        }

        m_glContext = SDL_GL_CreateContext(m_window);
        if (!m_glContext) {
            std::cerr << "[Sdl3Window] Failed to create GL context: " << SDL_GetError() << std::endl;
            return false;
        }

#if defined(__APPLE__)
        if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
            std::cerr << "[Sdl3Window] Failed to initialize GLAD: " << std::endl;
            return false;
        }
#else
        if (!gladLoadGLES2Loader((GLADloadproc)SDL_GL_GetProcAddress)) {
            std::cerr << "[Sdl3Window] Failed to initialize GLAD: " << std::endl;
            return false;
        }
#endif

        SDL_GL_SetSwapInterval(1);
        m_width = width;
        m_height = height;

        std::cout << "[Sdl3Window] Window created: " << width << "x" << height << std::endl;
        std::cout << "[Sdl3Window] OpenGL: " << glGetString(GL_VERSION) << std::endl;
        return true;
    }

    void shutdown() override {
        if (m_glContext) {
            SDL_GL_DestroyContext(m_glContext);
            m_glContext = nullptr;
        }
        if (m_window) {
            SDL_DestroyWindow(m_window);
            m_window = nullptr;
        }
    }

    void setKeepScreenOn(bool keep) override {
        if (keep) SDL_DisableScreenSaver();
        else SDL_EnableScreenSaver();
    }

    void setScreenBrightness(float brightness) override { (void)brightness; }

    void* getGlContext() override { return m_glContext; }

    void swapBuffers() override {
        if (m_window) SDL_GL_SwapWindow(m_window);
    }

    WindowInfo getWindowInfo() const override {
        WindowInfo info;
        if (m_window) {
            SDL_GetWindowSizeInPixels(m_window, &info.windowWidth, &info.windowHeight);
            info.screenWidth = info.windowWidth;
            info.screenHeight = info.windowHeight;
            info.pixelRatio = 1.0f;
            int logicalW, logicalH;
            SDL_GetWindowSize(m_window, &logicalW, &logicalH);
            if (logicalW > 0) info.pixelRatio = static_cast<float>(info.windowWidth) / logicalW;
            info.safeArea = { 0, 0, info.windowWidth, info.windowHeight, info.windowWidth, info.windowHeight };
        }
        return info;
    }

    SystemInfo getSystemInfo() const override {
        SystemInfo info;
        info.platform = "desktop";
        const char* platform = SDL_GetPlatform();
        if (platform) {
            info.system = platform;
            if (strcmp(platform, "Windows") == 0) info.platform = "windows";
            else if (strcmp(platform, "Mac OS X") == 0) info.platform = "mac";
            else if (strcmp(platform, "Linux") == 0) info.platform = "linux";
        }
        info.language = "en";
        info.benchmarkLevel = 100;
        return info;
    }

    void processEvent(const SDL_Event& event) {
        if (event.type == SDL_EVENT_WINDOW_RESIZED || event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
            SDL_GetWindowSizeInPixels(m_window, &m_width, &m_height);
            onWindowResize.emit(m_width, m_height);
        }
    }

private:
    SDL_Window* m_window = nullptr;
    SDL_GLContext m_glContext = nullptr;
    int m_width = 0, m_height = 0;
};

// ============================================================================
// SDL3 Input System
// ============================================================================

class Sdl3InputSystem : public IInputSystem {
public:
    bool initialize() override { return true; }
    void shutdown() override {}

    void showKeyboard(const KeyboardConfig& config) override { (void)config; }
    void hideKeyboard() override {}
    void vibrateShort(const std::string& type) override { (void)type; }
    void vibrateLong() override {}

    void processEvent(const SDL_Event& event) {
        switch (event.type) {
            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP: {
                if (event.key.repeat) break;
                KeyEvent ke;
                ke.type = (event.type == SDL_EVENT_KEY_DOWN) ? KeyEvent::Down : KeyEvent::Up;
                ke.key = SDL_GetKeyName(event.key.key);
                ke.code = SDL_GetScancodeName(event.key.scancode);
                ke.timestamp = static_cast<float>(event.key.timestamp) / 1e9f;
                onKey.emit(ke);
                break;
            }
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP: {
                MouseEvent me;
                me.type = (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) ? MouseEvent::Down : MouseEvent::Up;
                me.x = static_cast<float>(event.button.x);
                me.y = static_cast<float>(event.button.y);
                me.button = event.button.button - 1;
                me.timestamp = static_cast<float>(event.button.timestamp) / 1e9f;
                onMouse.emit(me);
                break;
            }
            case SDL_EVENT_MOUSE_MOTION: {
                MouseEvent me;
                me.type = MouseEvent::Move;
                me.x = static_cast<float>(event.motion.x);
                me.y = static_cast<float>(event.motion.y);
                me.deltaX = static_cast<float>(event.motion.xrel);
                me.deltaY = static_cast<float>(event.motion.yrel);
                me.timestamp = static_cast<float>(event.motion.timestamp) / 1e9f;
                onMouse.emit(me);
                break;
            }
            case SDL_EVENT_MOUSE_WHEEL: {
                MouseEvent me;
                me.type = MouseEvent::Wheel;
                me.x = static_cast<float>(event.wheel.mouse_x);
                me.y = static_cast<float>(event.wheel.mouse_y);
                me.deltaX = static_cast<float>(event.wheel.x);
                me.deltaY = static_cast<float>(event.wheel.y);
                me.timestamp = static_cast<float>(event.wheel.timestamp) / 1e9f;
                onMouse.emit(me);
                break;
            }
            case SDL_EVENT_FINGER_DOWN:
            case SDL_EVENT_FINGER_UP:
            case SDL_EVENT_FINGER_MOTION: {
                TouchEvent te;
                te.type = (event.type == SDL_EVENT_FINGER_DOWN) ? TouchEvent::Begin :
                          (event.type == SDL_EVENT_FINGER_UP) ? TouchEvent::End : TouchEvent::Move;
                te.id = static_cast<int>(event.tfinger.fingerID);
                te.x = event.tfinger.x;
                te.y = event.tfinger.y;
                te.timestamp = static_cast<float>(event.tfinger.timestamp) / 1e9f;
                onTouch.emit(te);
                break;
            }
            default: break;
        }
    }
};

// ============================================================================
// SDL3 Network System
// ============================================================================

class Sdl3NetworkSystem : public INetworkSystem {
public:
    ~Sdl3NetworkSystem() override { shutdown(); }

    bool initialize() override {
        m_isRunning = true;
        m_workerThread = std::thread(&Sdl3NetworkSystem::workerLoop, this);
        std::cout << "[Sdl3Network] Worker thread started" << std::endl;
        return true;
    }

    void shutdown() override {
        if (m_isRunning) {
            m_isRunning = false;
            m_taskCv.notify_all();
            if (m_workerThread.joinable()) m_workerThread.join();
        }
    }

    void sendHttpRequest(const HttpRequest& request, std::function<void(const HttpResponse&)> callback) override {
        auto handle = m_callbackManager.add(std::move(callback), nullptr, 15.0f);
        int id = static_cast<int>(handle.id());
        {
            std::lock_guard<std::mutex> lock(m_taskMutex);
            m_taskQueue.push({id, request});
        }
        m_taskCv.notify_one();
    }

    std::string getNetworkType() const override { return "wifi"; }

    void update(float dt) override {
        m_callbackManager.tick(dt);

        std::queue<HttpResult> results;
        {
            std::lock_guard<std::mutex> lock(m_resultMutex);
            std::swap(results, m_resultQueue);
        }
        while (!results.empty()) {
            m_callbackManager.invoke(results.front().callbackId, results.front().response);
            results.pop();
        }
    }

private:
    struct HttpTask { int callbackId; HttpRequest request; };
    struct HttpResult { int callbackId; HttpResponse response; };

    void workerLoop() {
        while (m_isRunning) {
            HttpTask task;
            {
                std::unique_lock<std::mutex> lock(m_taskMutex);
                m_taskCv.wait(lock, [this] { return !m_taskQueue.empty() || !m_isRunning; });
                if (!m_isRunning && m_taskQueue.empty()) break;
                task = m_taskQueue.front();
                m_taskQueue.pop();
            }

            HttpResponse response;
            response.success = false;
            response.statusCode = 501;
            response.errMsg = "HTTP client not available (CURL not linked)";

            {
                std::lock_guard<std::mutex> lock(m_resultMutex);
                m_resultQueue.push({task.callbackId, response});
            }
        }
    }

    CallbackManager<HttpResponse> m_callbackManager;
    std::atomic<bool> m_isRunning{false};
    std::thread m_workerThread;
    std::mutex m_taskMutex, m_resultMutex;
    std::queue<HttpTask> m_taskQueue;
    std::queue<HttpResult> m_resultQueue;
    std::condition_variable m_taskCv;
};

// ============================================================================
// SDL3 Storage System
// ============================================================================

class Sdl3StorageSystem : public IStorageSystem {
public:
    bool initialize() override { return true; }
    void shutdown() override {}

    void setItem(const std::string& key, const std::string& value) override { m_storage[key] = value; }
    std::string getItem(const std::string& key) const override {
        auto it = m_storage.find(key);
        return it != m_storage.end() ? it->second : "";
    }
    void removeItem(const std::string& key) override { m_storage.erase(key); }
    void clearAll() override { m_storage.clear(); }

private:
    mutable std::map<std::string, std::string> m_storage;
};

// ============================================================================
// SDL3 Platform Hub
// ============================================================================

class Sdl3PlatformHub : public IPlatformHub {
public:
    ~Sdl3PlatformHub() override { shutdown(); }

    bool initialize(const PlatformConfig& config) override {
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
            std::cerr << "[Sdl3Hub] SDL_Init failed: " << SDL_GetError() << std::endl;
            return false;
        }

        m_windowSystem = std::make_unique<Sdl3WindowSystem>();
        if (!static_cast<Sdl3WindowSystem*>(m_windowSystem.get())->initialize(config)) return false;

        m_inputSystem = std::make_unique<Sdl3InputSystem>();
        m_networkSystem = std::make_unique<Sdl3NetworkSystem>();
        m_storageSystem = std::make_unique<Sdl3StorageSystem>();

        if (!m_inputSystem->initialize()) return false;
        if (!m_networkSystem->initialize()) return false;
        if (!m_storageSystem->initialize()) return false;

        m_targetFps = config.targetFps;
        std::cout << "[Sdl3Hub] Initialized successfully" << std::endl;
        return true;
    }

    void runMainLoop(std::function<void(float)> updateFunc) override {
        m_isRunning = true;
        auto prevTime = std::chrono::high_resolution_clock::now();

        while (m_isRunning) {
            auto now = std::chrono::high_resolution_clock::now();
            float dt = std::chrono::duration<float>(now - prevTime).count();
            if (dt > 0.1f) dt = 0.1f;
            prevTime = now;

            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_EVENT_QUIT) exitApplication();
                static_cast<Sdl3WindowSystem*>(m_windowSystem.get())->processEvent(event);
                static_cast<Sdl3InputSystem*>(m_inputSystem.get())->processEvent(event);
            }

            m_networkSystem->update(dt);
            if (updateFunc) updateFunc(dt);
            m_windowSystem->swapBuffers();

            if (m_targetFps > 0) {
                float frameTime = 1.0f / m_targetFps;
                float elapsed = std::chrono::duration<float>(std::chrono::high_resolution_clock::now() - now).count();
                if (elapsed < frameTime) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>((frameTime - elapsed) * 1000)));
                }
            }
        }
    }

    void shutdown() override {
        if (m_storageSystem) m_storageSystem->shutdown();
        if (m_networkSystem) m_networkSystem->shutdown();
        if (m_inputSystem) m_inputSystem->shutdown();
        if (m_windowSystem) m_windowSystem->shutdown();
        SDL_Quit();
    }

    void exitApplication() override { m_isRunning = false; }

    IWindowSystem* getWindowSystem() const override { return m_windowSystem.get(); }
    IInputSystem* getInputSystem() const override { return m_inputSystem.get(); }
    IAudioSystem* getAudioSystem() const override { return nullptr; }
    INetworkSystem* getNetworkSystem() const override { return m_networkSystem.get(); }
    IStorageSystem* getStorageSystem() const override { return m_storageSystem.get(); }

private:
    bool m_isRunning = false;
    int m_targetFps = 60;
    std::unique_ptr<IWindowSystem> m_windowSystem;
    std::unique_ptr<IInputSystem> m_inputSystem;
    std::unique_ptr<INetworkSystem> m_networkSystem;
    std::unique_ptr<IStorageSystem> m_storageSystem;
};

std::unique_ptr<IPlatformHub> createPlatformSdl3() {
    return std::make_unique<Sdl3PlatformHub>();
}

} // namespace spt

#endif
