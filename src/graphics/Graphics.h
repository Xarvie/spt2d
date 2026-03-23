#pragma once

#include "../glad/glad.h"
#include "Shader.h"
#include "GameWork.h"

#include <memory>
#include <cstdint>
#include <type_traits>

namespace spt {

// =============================================================================
//  VertexPosColor
//
//  Interleaved vertex layout: 2D position + RGBA float colour.
//  Attribute locations match ShaderSources.h / Shader::kDefaultBindings:
//    location 0 – a_position  (x, y)
//    location 1 – a_color     (r, g, b, a)
// =============================================================================
struct VertexPosColor {
    float x, y;
    float r, g, b, a;
};
static_assert(std::is_trivially_copyable<VertexPosColor>::value);


// =============================================================================
//  DrawBatchCmd
//
//  Trivially-copyable RenderCommand payload for a geometry draw call.
//  Fits within RenderCommand::kPayloadSize (64 bytes).
//
//  Vertex data and the MVP matrix are stored in the owning GameWork's
//  uniformPool at record time.  `poolBase` is a raw pointer to that pool's
//  data() at the moment of recording.
//
//  Safety of the poolBase pointer
//  ───────────────────────────────
//  GameWork lives in a TripleBuffer slot.  The TripleBuffer guarantees that
//  the render thread's read slot is never written to by the logic thread, so
//  poolBase remains valid for the entire render frame.
// =============================================================================
struct DrawBatchCmd {
    GLuint          program;        //  4  GL program object
    GLuint          vao;            //  4  vertex array object
    GLuint          vbo;            //  4  vertex buffer object
    GLint           mvpLoc;         //  4  u_mvp uniform location (cached)
    uint32_t        vertexByteOff;  //  4  byte offset in pool → VertexPosColor[]
    uint32_t        vertexCount;    //  4  number of vertices (multiple of 3)
    uint32_t        mvpByteOff;     //  4  byte offset in pool → float[16]
    uint32_t        _pad0;          //  4  reserved
    const uint8_t*  poolBase;       //  8  pool data() pointer (stable per slot)
    //                               ────
    //                               36 bytes  < kPayloadSize (64)
};
static_assert(sizeof(DrawBatchCmd) <= RenderCommand::kPayloadSize,
              "DrawBatchCmd exceeds RenderCommand payload capacity");
static_assert(std::is_trivially_copyable<DrawBatchCmd>::value,
              "DrawBatchCmd must be trivially copyable");


// =============================================================================
//  Graphics
//
//  2D batch renderer.  Owns a single shader program, VAO, and VBO and
//  exposes two complementary rendering paths:
//
//  ┌─────────────────────────────────────────────────────────────────────────┐
//  │ Immediate-mode path  (render thread only – direct GL calls)             │
//  │                                                                         │
//  │   graphics.setMVP(mvp);                                                 │
//  │   graphics.drawTriangle(...);   // accumulate into CPU buffer           │
//  │   graphics.drawTriangle(...);                                           │
//  │   graphics.flush();             // upload + draw                        │
//  └─────────────────────────────────────────────────────────────────────────┘
//
//  ┌─────────────────────────────────────────────────────────────────────────┐
//  │ Record-mode path  (logic thread – no GL calls)                          │
//  │                                                                         │
//  │   // Inside IGameLogic::onRender(GameWork& work):                       │
//  │   graphics.recordTriangle(work, ...);                                   │
//  │   // → appends a DrawBatchCmd RenderCommand to work.renderCommands      │
//  │   // → copies vertex data + MVP into work.uniformPool                   │
//  │                                                                         │
//  │   // Render thread executes via RenderCommandExecutor::execute(work)    │
//  └─────────────────────────────────────────────────────────────────────────┘
//
//  Thread-safety
//  ──────────────
//  - initialize() / shutdown() / setMVP() / drawTriangle() / flush() :
//      render thread only (GL context must be current).
//  - recordTriangle() / recordTriangles() :
//      logic thread only during IGameLogic::onRender().  No GL calls are
//      made; the only shared state is the cached m_mvpUniformLoc which is
//      written once during initialize() and then read-only.
// =============================================================================
class Graphics {
public:
    /// Maximum vertices that can be batched in a single immediate-mode flush.
    static constexpr int kMaxImmediateVertices = 4096;

    /// Identity MVP provided as a convenience default for record-mode calls.
    static const float kIdentityMVP[16];

    Graphics()  = default;
    ~Graphics();

    Graphics(const Graphics&)            = delete;
    Graphics& operator=(const Graphics&) = delete;

    // -------------------------------------------------------------------------
    // Lifecycle  (render thread)
    // -------------------------------------------------------------------------

    /// Create the shader, VAO, and VBO.  Must be called once with a current
    /// GL context before any draw calls.  Returns false on GL error.
    bool initialize();

    /// Destroy all GL resources.  Safe to call without a prior initialize().
    void shutdown();

    [[nodiscard]] bool isInitialized() const noexcept { return m_initialized; }

    // -------------------------------------------------------------------------
    // Immediate-mode API  (render thread only)
    // -------------------------------------------------------------------------

    /// Replace the current MVP matrix.  The matrix is column-major (OpenGL
    /// convention).  Defaults to identity if never called.
    void setMVP(const float* mvp16) noexcept;

    void clear(float r, float g, float b, float a);

    void setViewport(int x, int y, int width, int height);

    /// Queue a triangle for the next flush().  Triggers an automatic flush if
    /// the internal vertex buffer is full.
    void drawTriangle(float x1, float y1, float r1, float g1, float b1,
                      float x2, float y2, float r2, float g2, float b2,
                      float x3, float y3, float r3, float g3, float b3);

    /// Upload all queued vertices to the VBO and issue a single draw call.
    /// No-op if no vertices have been queued since the last flush.
    void flush();

    // -------------------------------------------------------------------------
    // Record-mode API  (logic / onRender thread, no GL calls)
    //
    // Each call records a DrawBatchCmd RenderCommand into `work`.
    // Vertex data and `mvp` are copied into `work.uniformPool`; no GL state
    // is read or written.
    //
    // The recorded commands are executed by the render thread via
    // RenderCommandExecutor::execute(work).
    // -------------------------------------------------------------------------

    /// Record `count` vertices (must be a multiple of 3).
    /// \param mvp16   Column-major 4×4 MVP matrix.  Pass nullptr to use
    ///                the identity matrix (kIdentityMVP).
    /// \param sortKey RenderCommand sort key (see RenderCommand::buildSortKey).
    void recordTriangles(GameWork&             work,
                         const VertexPosColor* verts,
                         int                   count,
                         const float*          mvp16   = nullptr,
                         uint64_t              sortKey = 0);

    /// Convenience single-triangle overload.
    void recordTriangle(GameWork& work,
                        float x1, float y1, float r1, float g1, float b1,
                        float x2, float y2, float r2, float g2, float b2,
                        float x3, float y3, float r3, float g3, float b3,
                        const float* mvp16  = nullptr,
                        uint64_t     sortKey = 0);

    // -------------------------------------------------------------------------
    // Queries
    // -------------------------------------------------------------------------

    [[nodiscard]] GLuint vao()     const noexcept { return m_vao; }
    [[nodiscard]] GLuint program() const noexcept {
        return m_basicShader ? m_basicShader->program() : 0;
    }

private:
    // -------------------------------------------------------------------------
    // Internal
    // -------------------------------------------------------------------------
    bool createGLResources();

    /// RenderCommand execute function for DrawBatchCmd.
    static void execDrawBatch(const void* payload) noexcept;

    // -------------------------------------------------------------------------
    // GL resources
    // -------------------------------------------------------------------------
    GLuint                  m_vao = 0;
    GLuint                  m_vbo = 0;
    std::unique_ptr<Shader> m_basicShader;

    /// MVP uniform location, cached once during initialize().
    /// Written on the render thread at init, then read-only → safe to read
    /// from the logic thread during record calls.
    GLint m_mvpUniformLoc = -1;

    // -------------------------------------------------------------------------
    // Immediate-mode state  (render thread only)
    // -------------------------------------------------------------------------
    VertexPosColor m_immBuffer[kMaxImmediateVertices];
    int            m_immCount = 0;

    float m_mvp[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };

    bool m_initialized = false;
};

} // namespace spt
