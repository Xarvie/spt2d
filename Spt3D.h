// spt3d/Spt3D.h — Master public API
// C++17 · OpenGL ES 3.0 · Async producer/consumer · Handle-based resources
//
// Architecture:
//   Thread 1 (main/render): window events, GL context, command execution
//   Thread 2 (logic):       game update, command recording (zero GL calls)
//   Bridge: TripleBuffer<GameWork> — lock-free, zero-copy frame transfer
#pragma once

#include "src/Types.h"
#include "src/Handle.h"
#include "src/core/LinearAllocator.h"
#include "src/core/TripleBuffer.h"
#include "src/core/ThreadSafeQueue.h"
#include "src/core/Signal.h"
#include "src/core/ThreadConfig.h"
#include "src/core/RenderCommand.h"
#include "src/core/GameWork.h"
#include "src/core/ThreadModel.h"
#include "src/resource/MeshData.h"
#include "src/resource/MaterialSnapshot.h"
#include "src/resource/DrawList.h"
#include "src/resource/ResHandle.h"
#include "src/resource/IResource.h"
#include "src/resource/RawDataResource.h"
#include "src/resource/ResourceManager.h"
#include "src/vfs/VirtualFileSystem.h"
#include "src/vfs/IFileSystemProvider.h"
#include "src/gpu/GPUDevice.h"
#include "src/render/Executor.h"
#include "src/render/Pipeline.h"
#include "src/platform/Platform.h"

namespace spt3d {

// =========================================================================
//  App config
// =========================================================================

struct AppConfig {
    std::string_view title     = "SptGame";
    int              width     = 800;
    int              height    = 600;
    bool             highDpi   = true;
    bool             resizable = true;
    bool             vsync     = true;
    int              msaa      = 0;
    Color            clear     = Colors::Black;
    std::string_view canvas    = "#canvas";
};

// =========================================================================
//  Lifecycle
// =========================================================================

bool Init(const AppConfig& cfg, std::unique_ptr<IGameLogic> game);
bool Init(const AppConfig& cfg = {});
void Run();
void Run(std::function<void()> tick, int targetFps = 0);
void Shutdown();
void Quit();
bool ShouldQuit();

// =========================================================================
//  Window
// =========================================================================

int   ScreenW();
int   ScreenH();
int   RenderW();
int   RenderH();
float DPR();
bool  Focused();
bool  IsFullscreen();
void  ToggleFullscreen();

// =========================================================================
//  Timing (thread-safe reads)
// =========================================================================

float  Delta();
double Time();
int    FPS();

// =========================================================================
//  Platform events
// =========================================================================

Signal<int, int>&         OnResize();
Signal<>&                 OnFocusGain();
Signal<>&                 OnFocusLost();
Signal<>&                 OnQuit();
Signal<std::string_view>& OnDropFile();

// =========================================================================
//  VFS
// =========================================================================

void     MountAssets(std::string_view path);
void     MountUser(std::string_view path);
void     MountCache(std::string_view path);
uint64_t FsRead(std::string_view path, FsCallback cb);
uint64_t FsWrite(std::string_view path, const uint8_t* data, size_t size, FsCallback cb);
void     FsCancel(uint64_t id);
void     FsCancelAll();

// =========================================================================
//  Resource Manager (CPU resources)
// =========================================================================

ResourceManager& GetResourceManager();

// =========================================================================
//  GPU device & executor access (render thread, advanced use)
// =========================================================================

GPUDevice& GetGPU();
Executor&  GetExecutor();

// =========================================================================
//  Builtin shaders (handles valid after Init, safe from any thread)
// =========================================================================

ShaderHandle BuiltinShaderForward();
ShaderHandle BuiltinShaderUnlit();
ShaderHandle BuiltinShaderPBR();
ShaderHandle BuiltinShaderDepth();
ShaderHandle BuiltinShaderBlit();
ShaderHandle BuiltinShaderSkybox();

// =========================================================================
//  Builtin material snapshots
// =========================================================================

MaterialSnapshot DefaultMaterial();
MaterialSnapshot UnlitMaterial();
MaterialSnapshot PBRMaterial();

// =========================================================================
//  Pipeline presets
// =========================================================================

Pipeline ForwardPipeline();
Pipeline ForwardHDRPipeline();

// =========================================================================
//  Post effects — return StageDesc for Pipeline::pushStage()
// =========================================================================

StageDesc FxBloom(TexHandle src, RTHandle dst, float threshold = 1.0f,
                  float intensity = 1.0f, int iterations = 5);
StageDesc FxToneMapACES(TexHandle src, RTHandle dst = kNullRT);
StageDesc FxToneMapReinhard(TexHandle src, RTHandle dst = kNullRT);
StageDesc FxFXAA(TexHandle src, RTHandle dst = kNullRT);
StageDesc FxVignette(TexHandle src, RTHandle dst = kNullRT, float intensity = 0.5f);
StageDesc FxGrain(TexHandle src, RTHandle dst = kNullRT, float amount = 0.05f);
StageDesc FxGaussBlur(TexHandle src, RTHandle dst, float radius = 4.0f);

// =========================================================================
//  Builtin GLSL ES 300 shader sources
// =========================================================================

extern const char* kBlitVS;
extern const char* kBlitFS;
extern const char* kUnlitVS;
extern const char* kUnlitFS;
extern const char* kPhongVS;
extern const char* kPhongFS;
extern const char* kPBR_VS;
extern const char* kPBR_FS;
extern const char* kDepthVS;
extern const char* kDepthFS;
extern const char* kSkyboxVS;
extern const char* kSkyboxFS;

} // namespace spt3d
