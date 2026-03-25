#include "Spt3D.h"
#include "core/ThreadModel.h"
#include "core/RenderCommand.h"
#include "vfs/VirtualFileSystem.h"
#include "vfs/providers/NativeFileSystem.h"
#include "resource/ResourceManager.h"
#include "resource/MeshData.h"
#include "resource/DrawList.h"
#include "gpu/GPUDevice.h"
#include "gpu/BuiltinShaders.h"
#include "render/Executor.h"

#include <iostream>
#include <exception>
#include <memory>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

namespace spt3d {

static constexpr uint32_t kGLColorBufferBit = 0x00004000;
static constexpr uint32_t kGLDepthBufferBit = 0x00000100;

static std::unique_ptr<IPlatformHub> g_platform;
static std::unique_ptr<ThreadModel> g_threadModel;
static std::unique_ptr<ResourceManager> g_resourceManager;
static std::unique_ptr<GPUDevice> g_gpu;
static std::unique_ptr<Executor> g_executor;

IPlatformHub* GetPlatform() { return g_platform.get(); }

ResourceManager& GetResourceManager() {
    return *g_resourceManager;
}

GPUDevice& GetGPUDevice() {
    return *g_gpu;
}

class GameLogicImpl : public IGameLogic {
public:
    bool onInit() override {
        std::cout << "[Game] Initializing..." << std::endl;

        auto windowInfo = g_platform->getWindowSystem()->getWindowInfo();
        std::cout << "[Game] Window: " << windowInfo.screenWidth << "x" << windowInfo.screenHeight << std::endl;

        m_windowWidth = windowInfo.screenWidth;
        m_windowHeight = windowInfo.screenHeight;

#ifndef __EMSCRIPTEN__
        VirtualFileSystem::Instance().mount("file", 
            std::make_unique<NativeFileSystemProvider>("./"));
#endif

        if (!initGPU()) {
            std::cerr << "[Game] GPU initialization failed" << std::endl;
            return false;
        }

        m_running = true;
        std::cout << "[Game] Initialized successfully" << std::endl;
        return true;
    }

    void onUpdate(float dt, const InputFrame& input) override {
        m_time += dt;
        m_dt = dt;
        
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
        }
        
        g_resourceManager->update();
        VirtualFileSystem::Instance().processCompleted();
    }

    void onRender(GameWork& work) override {
        work.reset();
        work.setScreenSize(m_windowWidth, m_windowHeight);

        m_camera.position = Vec3(0.0f, 2.0f, 5.0f);
        m_camera.target = Vec3(0.0f, 0.0f, 0.0f);
        m_camera.up = Vec3(0.0f, 1.0f, 0.0f);
        m_camera.fov = 60.0f;
        m_camera.near_clip = 0.1f;
        m_camera.far_clip = 100.0f;
        m_camera.ortho = false;
        
        float aspect = static_cast<float>(m_windowWidth) / m_windowHeight;
        work.setCamera(m_camera, aspect, m_time, m_dt);

        buildClearCommand(work, kGLColorBufferBit | kGLDepthBufferBit,
                          0.1f, 0.1f, 0.2f, 1.0f, 1.0f, 0);

        float angle = m_time * 0.5f;
        Mat4 model = glm::rotate(Mat4(1.0f), angle, Vec3(0.0f, 1.0f, 0.0f));

        MaterialSnapshot mat;
        mat.shader = m_shader;
        mat.setVec4("u_color", Vec4(0.8f, 0.2f, 0.2f, 1.0f));
        mat.setTexture("u_texture", g_gpu->whiteTex(), 0);

        m_drawList.clear();
        m_drawList.push(m_cubeMesh, mat, model);
        m_drawList.emitCommands(work, SortMode::FrontToBack, m_camera.position);
    }

    void onShutdown() override {
        std::cout << "[Game] Shutting down..." << std::endl;
        m_drawList.clear();
        if (g_gpu && m_cubeMesh.value != 0) {
            g_gpu->destroyMesh(m_cubeMesh);
        }
        if (g_gpu && m_shader.value != 0) {
            g_gpu->destroyShader(m_shader);
        }
    }

    bool isRunning() const override {
        return m_running;
    }

private:
    bool initGPU() {
        if (!g_gpu->initialize()) {
            std::cerr << "[Game] GPUDevice::initialize() failed" << std::endl;
            return false;
        }

        std::cout << "[Game] GPU: " << g_gpu->caps().renderer << std::endl;
        std::cout << "[Game] Max Texture Size: " << g_gpu->caps().maxTexSize << std::endl;

        MeshData cubeData = GenCubeData(1.0f, 1.0f, 1.0f);
        m_cubeMesh = g_gpu->createMesh(cubeData);
        if (m_cubeMesh.value == 0) {
            std::cerr << "[Game] Failed to create cube mesh" << std::endl;
            return false;
        }

        ShaderDesc shaderDesc;
        ShaderPassDesc pass;
        pass.name = "FORWARD";
        pass.vs = shaders::kUnlitVS;
        pass.fs = shaders::kUnlitFS;
        shaderDesc.passes.push_back(pass);

        m_shader = g_gpu->createShader(shaderDesc);
        if (m_shader.value == 0) {
            std::cerr << "[Game] Failed to create shader" << std::endl;
            return false;
        }

        std::cout << "[Game] GPU resources created" << std::endl;
        return true;
    }

    bool m_running = false;
    int m_windowWidth = 1280;
    int m_windowHeight = 720;
    float m_time = 0.0f;
    float m_dt = 0.0f;

    MeshHandle m_cubeMesh;
    ShaderHandle m_shader;
    Camera3D m_camera;
    DrawList m_drawList;
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
    if (work && g_executor && g_gpu) {
        g_executor->execute(*work, *g_gpu);
    }
    
    g_threadModel->onFrameEnd();
}

} // namespace spt3d

int main(int argc, char* argv[]) {
    using namespace spt3d;

    g_resourceManager = std::make_unique<ResourceManager>();

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

    g_gpu = std::make_unique<GPUDevice>();
    g_executor = std::make_unique<Executor>(512);

    ThreadConfig threadConfig;
    threadConfig.multithreaded = false;

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
    g_gpu->shutdown();
    g_platform->shutdown();
#endif

    return 0;
}
