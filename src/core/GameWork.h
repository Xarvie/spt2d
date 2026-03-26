// spt3d/core/GameWork.h — Frame packet
// [THREAD: logic→write, render→read (const)]
//
// The logic thread fills a GameWork during onRender(), then publish() hands
// it to the render thread via TripleBuffer. The render thread reads it as
// const — it must NOT modify any field.
//
// Lifecycle:
//   Logic thread:  reset() → fill commands/uniforms → publish()
//   Render thread: fetch() → read commands → execute (const access only)
//
// All vectors retain capacity across frames (clear, not deallocate).
#pragma once

#include "RenderCommand.h"
#include "LinearAllocator.h"
#include "../Types.h"

#include <vector>
#include <cstdint>

namespace spt3d {

// =========================================================================
//  Viewport override
// =========================================================================

struct Viewport {
    int  x = 0, y = 0, w = 0, h = 0;
    bool valid = false;

    void set(int x_, int y_, int w_, int h_) noexcept {
        x = x_; y = y_; w = w_; h = h_; valid = true;
    }
    void clear() noexcept { *this = Viewport{}; }
};

// =========================================================================
//  PlatformCommand — IME, clipboard, etc.
// =========================================================================

struct PlatformCommand {
    enum class Type : uint8_t { StartIME, StopIME, SetIMERect, SetClipboard };
    Type        type     = Type::StopIME;
    int         x = 0, y = 0, w = 0, h = 0;
    const char* textData = nullptr;   // non-owning, caller manages lifetime within frame
};

// =========================================================================
//  GameWork
// =========================================================================

struct GameWork {

    // ── Frame metadata ──────────────────────────────────────────────────
    double   logicTime  = 0.0;
    float    deltaTime  = 0.0f;
    uint64_t frameIndex = 0;

    // ── Render commands (sorted by sortKey before execution) ────────────
    std::vector<RenderCommand> renderCommands;

    // ── Resource commands (processed BEFORE render commands) ────────────
    std::vector<ResourceCommand> resourceCommands;

    // ── Per-frame scratch memory (uniforms, vertex data, CPU resources) ─
    // Render commands store byte offsets into this pool.
    // Stable after publish() — render thread reads via const pointer.
    LinearAllocator uniformPool;

    // ── Built-in uniform data (auto-uploaded by executor) ──────────────
    PerFrameData frameData;
    LightBlock   lightBlock;

    // ── Platform commands ───────────────────────────────────────────────
    std::vector<PlatformCommand> platformCommands;

    // ── Viewport override ───────────────────────────────────────────────
    Viewport viewport;

    // ── Construction ────────────────────────────────────────────────────
    GameWork() {
        renderCommands.reserve(512);
        resourceCommands.reserve(32);
        platformCommands.reserve(8);
        uniformPool.reserve(8192);
    }

    GameWork(const GameWork&)            = default;
    GameWork& operator=(const GameWork&) = default;
    GameWork(GameWork&&)                 = default;
    GameWork& operator=(GameWork&&)      = default;

    // ── Reset (retains capacity) ────────────────────────────────────────
    void reset() noexcept {
        logicTime  = 0.0;
        deltaTime  = 0.0f;
        frameIndex = 0;
        renderCommands.clear();
        resourceCommands.clear();
        uniformPool.reset();
        frameData = PerFrameData{};
        lightBlock = LightBlock{};
        platformCommands.clear();
        viewport.clear();
    }

    // ── Submit helpers (logic thread only) ──────────────────────────────

    /// Submit a typed render command.
    template <typename T>
    void submit(const T& data, RenderCommand::ExecuteFn fn,
                uint64_t sortKey = 0, uint16_t typeId = T::kTypeId) {
        renderCommands.push_back(RenderCommand::Create(data, fn, sortKey, typeId));
    }

    /// Allocate scratch memory in the uniform pool and return the offset.
    /// The logic thread writes data at the returned pointer; the render
    /// thread later reads it via uniformPool.ptrAt<T>(offset).
    template <typename T>
    uint32_t allocUniform(const T& value) {
        void* p = uniformPool.alloc(sizeof(T), alignof(T));
        if (!p) return UINT32_MAX;
        std::memcpy(p, &value, sizeof(T));
        return uniformPool.offsetOf(p);
    }

    /// Allocate a raw byte block and return offset (for variable-size data).
    uint32_t allocBytes(const void* data, std::size_t size) {
        void* p = uniformPool.alloc(size, 8);
        if (!p) return UINT32_MAX;
        std::memcpy(p, data, size);
        return uniformPool.offsetOf(p);
    }

    /// Allocate an array of elements and return offset.
    uint32_t allocUniformArray(const void* data, std::size_t size) {
        void* p = uniformPool.alloc(size, 8);
        if (!p) return UINT32_MAX;
        std::memcpy(p, data, size);
        return uniformPool.offsetOf(p);
    }

    /// Submit a resource creation/destruction command.
    void submitResource(ResourceCommand cmd) {
        resourceCommands.push_back(cmd);
    }

    /// Set per-frame camera data (logic thread fills this in onRender).
    void setCamera(const Camera3D& cam, float aspect, float time, float dt) {
        frameData.view       = ViewMat(cam);
        frameData.projection = ProjMat(cam, aspect);
        frameData.viewProj   = frameData.projection * frameData.view;
        frameData.cameraPos  = cam.position;
        frameData.cameraDir  = glm::normalize(cam.target - cam.position);
        frameData.nearClip   = cam.near_clip;
        frameData.farClip    = cam.far_clip;
        frameData.time       = time;
        frameData.deltaTime  = dt;
    }

    void setScreenSize(int w, int h) {
        frameData.screenSize    = Vec2(float(w), float(h));
        frameData.screenSizeInv = Vec2(1.0f / float(w), 1.0f / float(h));
    }

    void setViewport(int x, int y, int w, int h) noexcept {
        viewport.set(x, y, w, h);
    }

    // ── Diagnostics ─────────────────────────────────────────────────────
    struct Stats {
        std::size_t renderCommandCount  = 0;
        std::size_t resourceCommandCount = 0;
        std::size_t uniformBytesUsed    = 0;
    };
    [[nodiscard]] Stats stats() const noexcept {
        return { renderCommands.size(), resourceCommands.size(), uniformPool.used() };
    }
};

} // namespace spt3d
