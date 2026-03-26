// spt3d/render/Executor.h — Render command executor
// [THREAD: render] — All methods run on the render thread only.
//
// Executor is the render-thread counterpart to the logic-thread's GameWork.
// It takes a const GameWork& and:
//   1. Processes resource commands (create/destroy GPU objects via GPUDevice)
//   2. Uploads built-in uniforms (camera, time, lights)
//   3. Sorts render commands by sortKey (stable sort)
//   4. Dispatches each command (bind shader, mesh, uniforms → draw)
//
// The executor never modifies GameWork. All GL state goes through GPUDevice.
#pragma once

#include "../core/GameWork.h"
#include "../gpu/GPUDevice.h"

#include <vector>
#include <cstddef>

namespace spt3d {

struct MaterialSnapshot;

// =========================================================================
//  RenderStateCache — avoids redundant GL state changes
// =========================================================================

class RenderStateCache {
public:
    void reset() noexcept;

    bool setProgram(uint32_t program) noexcept;
    bool setVAO(uint32_t vao)         noexcept;
    bool setBlend(bool enable)        noexcept;
    bool setDepthTest(bool enable)    noexcept;
    bool setDepthWrite(bool enable)   noexcept;

private:
    uint32_t m_program    = 0;
    uint32_t m_vao        = 0;
    bool     m_blend      = false;
    bool     m_depthTest  = false;
    bool     m_depthWrite = true;
};

// =========================================================================
//  Executor
// =========================================================================

class Executor {
public:
    explicit Executor(std::size_t reserveCapacity = 512);
    ~Executor() = default;

    Executor(const Executor&) = delete;
    Executor& operator=(const Executor&) = delete;

    /// Execute all commands in a GameWork frame.
    ///
    /// Steps:
    ///   1. gpu.processResourceCommands(work)
    ///   2. Upload PerFrameData + LightBlock as built-in uniforms
    ///   3. Apply viewport override if set
    ///   4. Stable-sort render commands by sortKey
    ///   5. Dispatch each command via its execFn (passing poolBase)
    ///
    /// The GameWork is accessed as const — no modification.
    void execute(const GameWork& work, GPUDevice& gpu);

    /// Reset driver-side state tracking. Call at frame start if external
    /// code may have changed GL state.
    void resetStateCache() noexcept;

    RenderStateCache& stateCache() noexcept { return m_stateCache; }

private:
    RenderStateCache         m_stateCache;
    std::vector<std::size_t> m_sortIndices;   // reused across frames, retains capacity
};

// =========================================================================
//  Built-in command execute functions
//
//  These are the ExecuteFn callbacks stored in RenderCommands.
//  They receive (payload, poolBase) and call GPUDevice/GL directly.
//  Defined in Executor.cpp.
// =========================================================================

namespace cmd {

void execDrawMesh(const void* payload, const uint8_t* poolBase) noexcept;
void execDrawMeshInstanced(const void* payload, const uint8_t* poolBase) noexcept;
void execClear(const void* payload, const uint8_t* poolBase) noexcept;
void execSetViewport(const void* payload, const uint8_t* poolBase) noexcept;
void execBindRT(const void* payload, const uint8_t* poolBase) noexcept;
void execBlit(const void* payload, const uint8_t* poolBase) noexcept;

} // namespace cmd

// =========================================================================
//  Command builder helpers (called by logic thread in onRender)
// =========================================================================

/// Submit a clear command into GameWork.
void buildClearCommand(GameWork& work, uint32_t mask,
                       float r = 0, float g = 0, float b = 0, float a = 1,
                       float depth = 1.0f, int stencil = 0,
                       uint64_t sortKey = 0);

/// Submit a viewport command into GameWork.
void buildViewportCommand(GameWork& work, int x, int y, int w, int h,
                          uint64_t sortKey = 0);

/// Submit a bind-render-target command into GameWork.
void buildBindRTCommand(GameWork& work, RTHandle rt, uint64_t sortKey = 0);

/// Submit a fullscreen blit command into GameWork.
void buildBlitCommand(GameWork& work, ShaderHandle shader,
                      const TexHandle* inputs, int inputCount,
                      RTHandle target,
                      const MaterialSnapshot* uniforms,
                      uint64_t sortKey);

} // namespace spt3d
