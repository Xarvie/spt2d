// spt3d/gpu/GPUDevice.h — GPU resource manager
// [THREAD: render] — ALL methods must be called on the render thread only.
//
// The sole owner of GL state. External code references GPU objects by handle;
// only GPUDevice can dereference a handle into a GL ID.
//
// Resource creation flow:
//   1. Logic thread submits ResourceCommand into GameWork.
//   2. Render thread calls processResourceCommands() before draw dispatch.
//   3. GPUDevice creates the GL object and maps handle → GL ID in its SlotMap.
//
// Pre-registration flow (for resources needed before first frame):
//   1. Call createMesh/createTexture/etc. directly (render thread only).
//   2. Returns handle immediately.
#pragma once

#include "../Handle.h"
#include "../Types.h"
#include "../resource/MeshData.h"

#include <cstdint>
#include <string_view>

namespace spt3d {

// Forward — defined in GameWork.h
struct GameWork;
struct MaterialUniforms;

// =========================================================================
//  Texture descriptor (for direct creation)
// =========================================================================

struct TexDesc {
    int    width    = 0;
    int    height   = 0;
    Fmt    format   = Fmt::RGBA8;
    Filter minFilter = Filter::Linear;
    Filter magFilter = Filter::Linear;
    Wrap   wrapU    = Wrap::Clamp;
    Wrap   wrapV    = Wrap::Clamp;
    bool   genMips  = false;
};

// =========================================================================
//  Shader descriptor
// =========================================================================

struct ShaderPassDesc {
    std::string name;           // "FORWARD", "SHADOW", "DEPTH", etc.
    std::string_view vs;        // GLSL source
    std::string_view fs;        // GLSL source
    std::optional<RenderState> stateOverride;
};

struct ShaderDesc {
    std::vector<ShaderPassDesc> passes;
};

// =========================================================================
//  Render target descriptor
// =========================================================================

struct RTDesc {
    int width  = 0;
    int height = 0;
    std::vector<Fmt> colorFormats;      // at least one
    Fmt  depthFormat = Fmt::D24S8;
    bool hasDepth    = true;
    float scale      = 1.0f;
};

// =========================================================================
//  GPU capabilities (filled on init)
// =========================================================================

struct GPUCaps {
    int  maxTexSize           = 0;
    int  maxTexUnits          = 0;
    int  maxColorAttachments  = 0;
    int  maxDrawBuffers       = 0;
    int  maxVertexAttribs     = 0;
    int  maxUniformBlockSize  = 0;
    bool floatTex             = false;
    bool floatLinear          = false;
    bool colorBufFloat        = false;
    bool etc2                 = false;
    bool astc                 = false;
    bool instancing           = false;
    bool depthTex             = false;
    bool srgb                 = false;
    std::string renderer;
    std::string vendor;
    std::string version;
};

// =========================================================================
//  GPUDevice
// =========================================================================

class GPUDevice {
public:
    GPUDevice();
    ~GPUDevice();

    GPUDevice(const GPUDevice&) = delete;
    GPUDevice& operator=(const GPUDevice&) = delete;

    /// Initialize GL state, query capabilities. Call once after GL context is current.
    bool initialize();

    /// Release all GPU resources. Call before GL context is destroyed.
    void shutdown();

    /// Query GPU capabilities (valid after initialize).
    [[nodiscard]] const GPUCaps& caps() const noexcept;

    // ── Resource creation (render thread only) ──────────────────────────

    MeshHandle   createMesh(const MeshData& data);
    TexHandle    createTexture(const TexDesc& desc, const void* pixels = nullptr);
    ShaderHandle createShader(const ShaderDesc& desc);
    RTHandle     createRenderTarget(const RTDesc& desc);

    // ── Resource destruction ────────────────────────────────────────────

    void destroyMesh(MeshHandle h);
    void destroyTexture(TexHandle h);
    void destroyShader(ShaderHandle h);
    void destroyRenderTarget(RTHandle h);

    // ── Handle validation ───────────────────────────────────────────────

    [[nodiscard]] bool isValid(MeshHandle h)   const;
    [[nodiscard]] bool isValid(TexHandle h)    const;
    [[nodiscard]] bool isValid(ShaderHandle h) const;
    [[nodiscard]] bool isValid(RTHandle h)     const;

    // ── Mesh queries ────────────────────────────────────────────────────

    [[nodiscard]] int  meshVertexCount(MeshHandle h) const;
    [[nodiscard]] int  meshIndexCount(MeshHandle h)  const;
    [[nodiscard]] bool meshIsIndexed(MeshHandle h)   const;

    // ── Render target queries ───────────────────────────────────────────

    [[nodiscard]] int       rtWidth(RTHandle h)        const;
    [[nodiscard]] int       rtHeight(RTHandle h)       const;
    [[nodiscard]] TexHandle rtColorTexture(RTHandle h, int index = 0) const;
    [[nodiscard]] TexHandle rtDepthTexture(RTHandle h) const;

    // ── Binding (set GL state for upcoming draw call) ───────────────────

    void bindMesh(MeshHandle h);
    void bindTexture(TexHandle h, int slot);
    void bindRenderTarget(RTHandle h);     // kNullRT = default framebuffer
    void bindDefaultFramebuffer();
    void useShader(ShaderHandle h, std::string_view passName = "FORWARD");

    // ── Shader uniform upload ───────────────────────────────────────────

    void setUniformInt  (ShaderHandle h, std::string_view name, int val);
    void setUniformFloat(ShaderHandle h, std::string_view name, float val);
    void setUniformVec2 (ShaderHandle h, std::string_view name, Vec2 val);
    void setUniformVec3 (ShaderHandle h, std::string_view name, Vec3 val);
    void setUniformVec4 (ShaderHandle h, std::string_view name, Vec4 val);
    void setUniformMat3 (ShaderHandle h, std::string_view name, const Mat3& val);
    void setUniformMat4 (ShaderHandle h, std::string_view name, const Mat4& val);

    /// Upload all uniforms from a MaterialUniforms block.
    void applyMaterialUniforms(ShaderHandle shader, const MaterialUniforms& u);

    /// Apply render state (blend, cull, depth, etc).
    void applyRenderState(const RenderState& state);

    // ── Draw calls ──────────────────────────────────────────────────────

    void drawIndexed(int indexCount);
    void drawArrays(int vertexCount);
    void drawFullscreenTriangle();      // uses internal VAO
    
    // ── Instanced draw calls ────────────────────────────────────────────
    
    void drawIndexedInstanced(int indexCount, int instanceCount);
    void drawArraysInstanced(int vertexCount, int instanceCount);
    
    // ── Instance buffer ─────────────────────────────────────────────────
    
    /// Set per-instance data for instanced rendering.
    /// @param mesh The mesh to attach instance data to
    /// @param data Pointer to instance data
    /// @param size Size of instance data in bytes
    /// @param stride Stride of each instance's data
    void setInstanceBuffer(MeshHandle mesh, const void* data, size_t size, size_t stride);
    
    // ── MRT (Multiple Render Targets) ───────────────────────────────────
    
    /// Set which color attachments to draw to.
    /// @param count Number of active draw buffers
    /// @param attachments Array of attachment indices (0 = GL_COLOR_ATTACHMENT0)
    void drawBuffers(int count, const int* attachments);

    // ── Batch resource command processing ───────────────────────────────

    /// Process all ResourceCommands in the GameWork. Called by Executor
    /// before draw dispatch. Creates/destroys GPU objects as requested.
    void processResourceCommands(const GameWork& work);

    // ── Built-in resources ──────────────────────────────────────────────

    /// A 1x1 white texture, created during initialize().
    [[nodiscard]] TexHandle whiteTex()  const noexcept;
    [[nodiscard]] TexHandle blackTex()  const noexcept;
    [[nodiscard]] TexHandle normalTex() const noexcept;

    /// Fullscreen triangle mesh (3 verts, no index buffer).
    [[nodiscard]] MeshHandle fullscreenTriMesh() const noexcept;

private:
    struct Impl;
    Impl* m_impl = nullptr;     // pimpl — Impl defined in GPUDevice.cpp
};

} // namespace spt3d
