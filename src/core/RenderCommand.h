// spt3d/core/RenderCommand.h — Render & resource command definitions
// [THREAD: logic→write, render→read]
//
// Two command categories flow through GameWork:
//
//   1. ResourceCommand — GPU resource creation/destruction requests.
//      The logic thread prepares CPU-side data and submits a command.
//      The render thread processes them BEFORE draw commands.
//
//   2. RenderCommand — Draw calls, state changes, clears.
//      Sorted by sortKey before execution on the render thread.
//
// All payload structs must be trivially copyable and fit within kPayloadSize.
// Larger data (vertex arrays, pixel buffers) lives in GameWork::uniformPool
// and is referenced by byte offset.
#pragma once

#include "../Handle.h"
#include "../Types.h"
#include "LinearAllocator.h"

#include <cstdint>
#include <cstring>
#include <type_traits>

namespace spt3d {

// =========================================================================
//  RenderCommand — sorted draw/state command
// =========================================================================

struct RenderCommand {
    static constexpr std::size_t kPayloadSize = 64;
    using ExecuteFn = void (*)(const void* payload, const uint8_t* poolBase);

    ExecuteFn execFn  = nullptr;
    uint64_t  sortKey = 0;
    uint16_t  typeId  = 0;
    uint8_t   _pad[6] = {};
    alignas(8) uint8_t payload[kPayloadSize] = {};

    void execute(const uint8_t* poolBase) const {
        if (execFn) execFn(payload, poolBase);
    }

    // Sort key layout (64 bits, high = most significant):
    //   [63:56] layer       (0-255)
    //   [55:32] depth       (24-bit fixed-point)
    //   [31:16] material id
    //   [15: 0] sequence
    static constexpr uint64_t BuildSortKey(uint8_t layer, uint32_t depth24,
                                           uint16_t matId, uint16_t seq) noexcept {
        return (uint64_t(layer)           << 56)
             | (uint64_t(depth24 & 0x00FFFFFFu) << 32)
             | (uint64_t(matId)           << 16)
             |  uint64_t(seq);
    }

    template <typename T>
    static RenderCommand Create(const T& data, ExecuteFn fn,
                                uint64_t key = 0, uint16_t tid = 0) noexcept {
        static_assert(sizeof(T) <= kPayloadSize, "Payload too large");
        static_assert(std::is_trivially_copyable<T>::value, "Payload must be trivially copyable");
        RenderCommand cmd;
        std::memcpy(cmd.payload, &data, sizeof(T));
        cmd.execFn  = fn;
        cmd.sortKey = key;
        cmd.typeId  = tid;
        return cmd;
    }
};

static_assert(std::is_trivially_copyable<RenderCommand>::value);

// =========================================================================
//  Draw command payloads
// =========================================================================

/// Draw a mesh with a material snapshot.
/// All references are handles or pool offsets — no raw pointers.
struct DrawMeshCmd {
    static constexpr uint16_t kTypeId = 1;
    MeshHandle   mesh;
    ShaderHandle shader;
    uint32_t     uniformsOffset;    // byte offset into uniformPool → MaterialUniforms
    uint32_t     transformOffset;   // byte offset into uniformPool → Mat4 (model matrix)
    uint64_t     _reserved = 0;
};
static_assert(sizeof(DrawMeshCmd) <= RenderCommand::kPayloadSize);
static_assert(std::is_trivially_copyable<DrawMeshCmd>::value);

/// Clear render target.
struct ClearCmd {
    static constexpr uint16_t kTypeId = 10;
    uint32_t mask      = 0;         // GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT
    float    color[4]  = {0,0,0,1};
    float    depth     = 1.0f;
    int      stencil   = 0;
};
static_assert(sizeof(ClearCmd) <= RenderCommand::kPayloadSize);
static_assert(std::is_trivially_copyable<ClearCmd>::value);

/// Set viewport.
struct SetViewportCmd {
    static constexpr uint16_t kTypeId = 11;
    int x = 0, y = 0, w = 0, h = 0;
};
static_assert(sizeof(SetViewportCmd) <= RenderCommand::kPayloadSize);
static_assert(std::is_trivially_copyable<SetViewportCmd>::value);

/// Bind a render target.
struct BindRTCmd {
    static constexpr uint16_t kTypeId = 12;
    RTHandle rt;                    // kNullRT = default framebuffer
};
static_assert(sizeof(BindRTCmd) <= RenderCommand::kPayloadSize);
static_assert(std::is_trivially_copyable<BindRTCmd>::value);

/// Fullscreen blit (post-processing pass).
struct BlitCmd {
    static constexpr uint16_t kTypeId = 13;
    ShaderHandle shader;
    TexHandle    inputs[4];         // up to 4 input textures
    int          inputCount = 0;
    RTHandle     target;            // kNullRT = screen
    uint32_t     uniformsOffset;    // byte offset into uniformPool
};
static_assert(sizeof(BlitCmd) <= RenderCommand::kPayloadSize);
static_assert(std::is_trivially_copyable<BlitCmd>::value);

// =========================================================================
//  MaterialUniforms — flat, fixed-size uniform block stored in uniformPool
// =========================================================================
//
// Stored in LinearAllocator by the logic thread, read by the render thread.
// No heap allocation, no pointers — just flat arrays of values.
//
// The logic thread writes this once per draw call, the render thread reads
// it during command execution. Thread safety is guaranteed by TripleBuffer
// slot ownership (they never alias).

struct MaterialUniforms {
    static constexpr int kMaxTextures = 8;
    static constexpr int kMaxFloats   = 32;
    static constexpr int kMaxVec4s    = 16;
    static constexpr int kMaxMat4s    = 4;

    // Texture bindings
    struct TexSlot { TexHandle tex; int32_t nameHash = 0; };
    TexSlot  textures[kMaxTextures] = {};
    int      texCount = 0;

    // Scalar uniforms (hashed name → value)
    struct FloatEntry { int32_t nameHash = 0; float value = 0; };
    FloatEntry floats[kMaxFloats] = {};
    int        floatCount = 0;

    // Vector uniforms
    struct Vec4Entry { int32_t nameHash = 0; Vec4 value{0}; };
    Vec4Entry vec4s[kMaxVec4s] = {};
    int       vec4Count = 0;

    // Matrix uniforms
    struct Mat4Entry { int32_t nameHash = 0; Mat4 value{1.0f}; };
    Mat4Entry mat4s[kMaxMat4s] = {};
    int       mat4Count = 0;

    // Render state snapshot
    RenderState state;
    std::string_view tag;    // valid only within the frame (points into pooled string)
};

// =========================================================================
//  ResourceCommand — GPU resource creation/destruction
// =========================================================================

enum class ResourceCmdType : uint8_t {
    CreateMesh,
    CreateTexture,
    CreateShader,
    CreateRenderTarget,
    DestroyMesh,
    DestroyTexture,
    DestroyShader,
    DestroyRenderTarget,
    UpdateTexture,
};

struct ResourceCommand {
    ResourceCmdType type;
    uint32_t        handle;         // pre-allocated handle for creation; existing handle for destroy
    uint32_t        dataOffset;     // byte offset into uniformPool where CPU-side data lives
    uint32_t        dataSize;       // size in bytes
};

// =========================================================================
//  PerFrameData — built-in uniforms the executor auto-uploads
// =========================================================================

struct PerFrameData {
    Mat4  view        {1.0f};
    Mat4  projection  {1.0f};
    Mat4  viewProj    {1.0f};
    Vec3  cameraPos   {0};
    Vec3  cameraDir   {0,0,1};
    Vec2  screenSize  {0};
    Vec2  screenSizeInv{0};
    float time        = 0;
    float deltaTime   = 0;
    float nearClip    = 0.1f;
    float farClip     = 1000.0f;
};

// =========================================================================
//  LightBlock — auto-injected into shaders
// =========================================================================

struct LightBlock {
    static constexpr int kMaxLights = 16;
    Light lights[kMaxLights] = {};
    int   count = 0;
    Color ambientColor = Colors::White;
    float ambientIntensity = 0.1f;
};

} // namespace spt3d
