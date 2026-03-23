#include "Graphics.h"
#include "ShaderSources.h"

#include <iostream>
#include <cstring>
#include <cassert>

namespace spt {

// =============================================================================
//  Static data
// =============================================================================

const float Graphics::kIdentityMVP[16] = {
    1.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 1.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 1.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 1.0f,
};

// =============================================================================
//  Lifecycle
// =============================================================================

Graphics::~Graphics() {
    shutdown();
}

bool Graphics::initialize() {
    assert(!m_initialized && "Graphics::initialize() called twice");

    if (!createGLResources()) {
        std::cerr << "[Graphics] Failed to create GL resources\n";
        shutdown();   // clean up any partial resources
        return false;
    }

    m_initialized = true;
    std::cout << "[Graphics] Initialized\n";
    return true;
}

void Graphics::shutdown() {
    // Flush any pending immediate-mode geometry so nothing is silently lost.
    if (m_immCount > 0) flush();

    if (m_vbo != 0) { glDeleteBuffers(1, &m_vbo);       m_vbo = 0; }
    if (m_vao != 0) { glDeleteVertexArrays(1, &m_vao);  m_vao = 0; }

    m_basicShader.reset();
    m_mvpUniformLoc = -1;
    m_immCount      = 0;
    m_initialized   = false;
}

bool Graphics::createGLResources() {
    // ---- Shader -------------------------------------------------------------
    m_basicShader = std::make_unique<Shader>();
    if (!m_basicShader->loadFromSource(SHADER_VS_BASIC, SHADER_FS_BASIC)) {
        std::cerr << "[Graphics] Shader error:\n"
                  << m_basicShader->lastError() << '\n';
        return false;
    }

    // Cache the MVP uniform location.  Done once here on the render thread;
    // the cached value is then read (never written) by the logic thread during
    // record calls, which is safe.
    m_mvpUniformLoc = m_basicShader->uniformLocation("u_mvp");

    // ---- VAO + VBO ----------------------------------------------------------
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);

    // Pre-allocate VBO storage large enough for the maximum immediate-mode
    // batch.  GL_DYNAMIC_DRAW signals the driver that data will be uploaded
    // repeatedly, favoring driver-managed placement in fast write-through RAM.
    glBufferData(GL_ARRAY_BUFFER,
                 kMaxImmediateVertices * static_cast<GLsizeiptr>(sizeof(VertexPosColor)),
                 nullptr, GL_DYNAMIC_DRAW);

    // a_position : location 0, vec2
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE,
                          sizeof(VertexPosColor),
                          reinterpret_cast<const void*>(offsetof(VertexPosColor, x)));

    // a_color : location 1, vec4
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE,
                          sizeof(VertexPosColor),
                          reinterpret_cast<const void*>(offsetof(VertexPosColor, r)));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    // Check for GL errors introduced during setup.
    const GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        std::cerr << "[Graphics] GL error during setup: 0x"
                  << std::hex << err << std::dec << '\n';
        return false;
    }

    return true;
}

// =============================================================================
//  Immediate-mode API  (render thread only)
// =============================================================================

void Graphics::setMVP(const float* mvp16) noexcept {
    assert(mvp16 && "setMVP(): null pointer");
    std::memcpy(m_mvp, mvp16, 16 * sizeof(float));
}

void Graphics::clear(float r, float g, float b, float a) {
    glClearColor(r, g, b, a);
    glClear(GL_COLOR_BUFFER_BIT);
}

void Graphics::setViewport(int x, int y, int width, int height) {
    glViewport(x, y, width, height);
}

void Graphics::drawTriangle(float x1, float y1, float r1, float g1, float b1,
                             float x2, float y2, float r2, float g2, float b2,
                             float x3, float y3, float r3, float g3, float b3) {
    assert(m_initialized && "drawTriangle() called before initialize()");

    if (m_immCount + 3 > kMaxImmediateVertices) {
        flush();
    }

    VertexPosColor* v = &m_immBuffer[m_immCount];
    v[0] = {x1, y1, r1, g1, b1, 1.0f};
    v[1] = {x2, y2, r2, g2, b2, 1.0f};
    v[2] = {x3, y3, r3, g3, b3, 1.0f};
    m_immCount += 3;
}

void Graphics::flush() {
    if (m_immCount == 0) return;
    assert(m_initialized && "flush() called before initialize()");

    m_basicShader->use();
    if (m_mvpUniformLoc != -1) {
        glUniformMatrix4fv(m_mvpUniformLoc, 1, GL_FALSE, m_mvp);
    }

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    m_immCount * static_cast<GLsizeiptr>(sizeof(VertexPosColor)),
                    m_immBuffer);
    glDrawArrays(GL_TRIANGLES, 0, m_immCount);

    m_immCount = 0;
}

// =============================================================================
//  Record-mode API  (logic thread, no GL calls)
// =============================================================================

void Graphics::recordTriangles(GameWork&             work,
                                const VertexPosColor* verts,
                                int                   count,
                                const float*          mvp16,
                                uint64_t              sortKey) {
    assert(verts    && "recordTriangles(): null vertex pointer");
    assert(count > 0 && count % 3 == 0
           && "recordTriangles(): count must be a positive multiple of 3");

    if (!mvp16) mvp16 = kIdentityMVP;

    // ---- Allocate vertex data in the frame's uniform pool ------------------
    // The pool is a bump allocator that never reallocates once it has reached
    // its steady-state capacity, so alloc() is typically just a pointer bump.
    const std::size_t vertexBytes = static_cast<std::size_t>(count) * sizeof(VertexPosColor);
    void* vertDst = work.uniformPool.alloc(vertexBytes, alignof(VertexPosColor));
    if (!vertDst) {
        std::cerr << "[Graphics] uniformPool OOM – skipping draw call\n";
        return;
    }
    std::memcpy(vertDst, verts, vertexBytes);

    // ---- Allocate MVP in the pool -------------------------------------------
    void* mvpDst = work.uniformPool.alloc(16 * sizeof(float), alignof(float));
    if (!mvpDst) {
        std::cerr << "[Graphics] uniformPool OOM (MVP) – skipping draw call\n";
        return;
    }
    std::memcpy(mvpDst, mvp16, 16 * sizeof(float));

    // ---- Build the command payload ------------------------------------------
    // poolBase is captured now.  It is valid for the entire render frame
    // because the TripleBuffer guarantees the read slot is never modified
    // while the render thread holds it.
    const uint8_t* poolBase = work.uniformPool.data();

    DrawBatchCmd cmd{};
    cmd.program       = program();
    cmd.vao           = m_vao;
    cmd.vbo           = m_vbo;
    cmd.mvpLoc        = m_mvpUniformLoc;       // read-only after initialize()
    cmd.vertexByteOff = work.uniformPool.offsetOf(vertDst);
    cmd.vertexCount   = static_cast<uint32_t>(count);
    cmd.mvpByteOff    = work.uniformPool.offsetOf(mvpDst);
    cmd._pad0         = 0;
    cmd.poolBase      = poolBase;

    work.renderCommands.push_back(
        RenderCommand::create(cmd, &Graphics::execDrawBatch, sortKey));
}

void Graphics::recordTriangle(GameWork& work,
                               float x1, float y1, float r1, float g1, float b1,
                               float x2, float y2, float r2, float g2, float b2,
                               float x3, float y3, float r3, float g3, float b3,
                               const float* mvp16,
                               uint64_t     sortKey) {
    const VertexPosColor verts[3] = {
        {x1, y1, r1, g1, b1, 1.0f},
        {x2, y2, r2, g2, b2, 1.0f},
        {x3, y3, r3, g3, b3, 1.0f},
    };
    recordTriangles(work, verts, 3, mvp16, sortKey);
}

// =============================================================================
//  RenderCommand execute function
//
//  Called by RenderCommandExecutor on the render thread.
//  Must not call any methods on Graphics (it does not have a `this` pointer).
//  All data it needs is encoded in the payload or the GL context.
// =============================================================================
void Graphics::execDrawBatch(const void* payload) noexcept {
    const auto& cmd = *static_cast<const DrawBatchCmd*>(payload);

    // Reconstruct typed pointers from pool-relative byte offsets.
    const auto* verts = reinterpret_cast<const VertexPosColor*>(
                            cmd.poolBase + cmd.vertexByteOff);
    const auto* mvp   = reinterpret_cast<const float*>(
                            cmd.poolBase + cmd.mvpByteOff);

    glUseProgram(cmd.program);

    if (cmd.mvpLoc != -1) {
        glUniformMatrix4fv(cmd.mvpLoc, 1, GL_FALSE, mvp);
    }

    glBindVertexArray(cmd.vao);
    glBindBuffer(GL_ARRAY_BUFFER, cmd.vbo);

    // Upload vertices for this batch.  Commands are executed sequentially so
    // the previous batch's data is already consumed by the GPU by the time
    // we overwrite offset 0.  Using offset 0 avoids the need to track a
    // running VBO offset across commands.
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    cmd.vertexCount * static_cast<GLsizeiptr>(sizeof(VertexPosColor)),
                    verts);

    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(cmd.vertexCount));
}

} // namespace spt
