#include "Graphics.h"
#include "ShaderSources.h"

#include <iostream>
#include <cstring>
#include <cassert>

namespace spt {

const float Graphics::kIdentityMVP[16] = {
    1.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 1.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 1.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 1.0f,
};

Graphics::~Graphics() {
    shutdown();
}

bool Graphics::initialize() {
    assert(!m_initialized && "Graphics::initialize() called twice");

    if (!createGLResources()) {
        std::cerr << "[Graphics] Failed to create GL resources\n";
        shutdown();
        return false;
    }

    if (!createTextureGLResources()) {
        std::cerr << "[Graphics] Failed to create texture GL resources\n";
        shutdown();
        return false;
    }

    m_initialized = true;
    std::cout << "[Graphics] Initialized\n";
    return true;
}

void Graphics::shutdown() {
    if (m_immCount > 0) flush();
    if (m_texImmCount > 0) flushTextures();

    if (m_vbo != 0) { glDeleteBuffers(1, &m_vbo);       m_vbo = 0; }
    if (m_vao != 0) { glDeleteVertexArrays(1, &m_vao);  m_vao = 0; }
    m_basicShader.reset();
    m_mvpUniformLoc = -1;
    m_immCount = 0;

    if (m_texVbo != 0) { glDeleteBuffers(1, &m_texVbo);     m_texVbo = 0; }
    if (m_texVao != 0) { glDeleteVertexArrays(1, &m_texVao); m_texVao = 0; }
    m_textureShader.reset();
    m_texMvpUniformLoc = -1;
    m_texUniformLoc = -1;
    m_texImmCount = 0;
    m_currentTexture = 0;

    m_initialized = false;
}

bool Graphics::createGLResources() {
    m_basicShader = std::make_unique<Shader>();
    if (!m_basicShader->loadFromSource(SHADER_VS_BASIC, SHADER_FS_BASIC)) {
        std::cerr << "[Graphics] Shader error:\n"
                  << m_basicShader->lastError() << '\n';
        return false;
    }

    m_mvpUniformLoc = m_basicShader->uniformLocation("u_mvp");

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);

    glBufferData(GL_ARRAY_BUFFER,
                 kMaxImmediateVertices * static_cast<GLsizeiptr>(sizeof(VertexPosColor)),
                 nullptr, GL_DYNAMIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE,
                          sizeof(VertexPosColor),
                          reinterpret_cast<const void*>(offsetof(VertexPosColor, x)));

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE,
                          sizeof(VertexPosColor),
                          reinterpret_cast<const void*>(offsetof(VertexPosColor, r)));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    const GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        std::cerr << "[Graphics] GL error during setup: 0x"
                  << std::hex << err << std::dec << '\n';
        return false;
    }

    return true;
}

bool Graphics::createTextureGLResources() {
    m_textureShader = std::make_unique<Shader>();
    if (!m_textureShader->loadFromSource(SHADER_VS_TEXTURE, SHADER_FS_TEXTURE)) {
        std::cerr << "[Graphics] Texture shader error:\n"
                  << m_textureShader->lastError() << '\n';
        return false;
    }

    m_texMvpUniformLoc = m_textureShader->uniformLocation("u_mvp");
    m_texUniformLoc = m_textureShader->uniformLocation("u_texture");

    glGenVertexArrays(1, &m_texVao);
    glGenBuffers(1, &m_texVbo);

    glBindVertexArray(m_texVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_texVbo);

    glBufferData(GL_ARRAY_BUFFER,
                 kMaxTextureVertices * static_cast<GLsizeiptr>(sizeof(VertexPosColorTex)),
                 nullptr, GL_DYNAMIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE,
                          sizeof(VertexPosColorTex),
                          reinterpret_cast<const void*>(offsetof(VertexPosColorTex, x)));

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE,
                          sizeof(VertexPosColorTex),
                          reinterpret_cast<const void*>(offsetof(VertexPosColorTex, u)));

    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE,
                          sizeof(VertexPosColorTex),
                          reinterpret_cast<const void*>(offsetof(VertexPosColorTex, r)));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    const GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        std::cerr << "[Graphics] GL error during texture setup: 0x"
                  << std::hex << err << std::dec << '\n';
        return false;
    }

    return true;
}

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

void Graphics::drawTexture(GLuint texture, float x, float y, float w, float h,
                            float r, float g, float b, float a) {
    drawTextureRect(texture, x, y, w, h, 0.0f, 0.0f, 1.0f, 1.0f, r, g, b, a);
}

void Graphics::drawTextureRect(GLuint texture, float x, float y, float w, float h,
                                float u0, float v0, float u1, float v1,
                                float r, float g, float b, float a) {
    assert(m_initialized && "drawTextureRect() called before initialize()");

    if (m_texImmCount + 6 > kMaxTextureVertices || m_currentTexture != texture) {
        flushTextures();
        m_currentTexture = texture;
    }

    VertexPosColorTex* v = &m_texImmBuffer[m_texImmCount];

    v[0] = {x,     y,     u0, v0, r, g, b, a};
    v[1] = {x + w, y,     u1, v0, r, g, b, a};
    v[2] = {x + w, y + h, u1, v1, r, g, b, a};

    v[3] = {x,     y,     u0, v0, r, g, b, a};
    v[4] = {x + w, y + h, u1, v1, r, g, b, a};
    v[5] = {x,     y + h, u0, v1, r, g, b, a};

    m_texImmCount += 6;
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

void Graphics::flushTextures() {
    if (m_texImmCount == 0) return;
    assert(m_initialized && "flushTextures() called before initialize()");

    m_textureShader->use();
    if (m_texMvpUniformLoc != -1) {
        glUniformMatrix4fv(m_texMvpUniformLoc, 1, GL_FALSE, m_mvp);
    }
    if (m_texUniformLoc != -1) {
        glUniform1i(m_texUniformLoc, 0);
    }

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_currentTexture);

    glBindVertexArray(m_texVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_texVbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    m_texImmCount * static_cast<GLsizeiptr>(sizeof(VertexPosColorTex)),
                    m_texImmBuffer);
    glDrawArrays(GL_TRIANGLES, 0, m_texImmCount);

    m_texImmCount = 0;
    m_currentTexture = 0;
}

void Graphics::recordTriangles(GameWork&             work,
                                const VertexPosColor* verts,
                                int                   count,
                                const float*          mvp16,
                                uint64_t              sortKey) {
    assert(verts    && "recordTriangles(): null vertex pointer");
    assert(count > 0 && count % 3 == 0
           && "recordTriangles(): count must be a positive multiple of 3");

    if (!mvp16) mvp16 = kIdentityMVP;

    const std::size_t vertexBytes = static_cast<std::size_t>(count) * sizeof(VertexPosColor);
    void* vertDst = work.uniformPool.alloc(vertexBytes, alignof(VertexPosColor));
    if (!vertDst) {
        std::cerr << "[Graphics] uniformPool OOM – skipping draw call\n";
        return;
    }
    std::memcpy(vertDst, verts, vertexBytes);

    void* mvpDst = work.uniformPool.alloc(16 * sizeof(float), alignof(float));
    if (!mvpDst) {
        std::cerr << "[Graphics] uniformPool OOM (MVP) – skipping draw call\n";
        return;
    }
    std::memcpy(mvpDst, mvp16, 16 * sizeof(float));

    const uint8_t* poolBase = work.uniformPool.data();

    DrawBatchCmd cmd{};
    cmd.program       = program();
    cmd.vao           = m_vao;
    cmd.vbo           = m_vbo;
    cmd.mvpLoc        = m_mvpUniformLoc;
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

void Graphics::recordTexture(GameWork& work, GLuint texture,
                              float x, float y, float w, float h,
                              const float* mvp16,
                              uint64_t sortKey) {
    assert(m_initialized && "recordTexture() called before initialize()");

    if (!mvp16) mvp16 = kIdentityMVP;

    const VertexPosColorTex verts[6] = {
        {x,     y,     0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f},
        {x + w, y,     1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f},
        {x + w, y + h, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f},
        {x,     y,     0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f},
        {x + w, y + h, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f},
        {x,     y + h, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f},
    };

    const std::size_t vertexBytes = 6 * sizeof(VertexPosColorTex);
    void* vertDst = work.uniformPool.alloc(vertexBytes, alignof(VertexPosColorTex));
    if (!vertDst) {
        std::cerr << "[Graphics] uniformPool OOM – skipping texture draw call\n";
        return;
    }
    std::memcpy(vertDst, verts, vertexBytes);

    void* mvpDst = work.uniformPool.alloc(16 * sizeof(float), alignof(float));
    if (!mvpDst) {
        std::cerr << "[Graphics] uniformPool OOM (MVP) – skipping texture draw call\n";
        return;
    }
    std::memcpy(mvpDst, mvp16, 16 * sizeof(float));

    const uint8_t* poolBase = work.uniformPool.data();

    DrawTextureCmd cmd{};
    cmd.program       = textureProgram();
    cmd.vao           = m_texVao;
    cmd.vbo           = m_texVbo;
    cmd.texture       = texture;
    cmd.mvpLoc        = m_texMvpUniformLoc;
    cmd.texLoc        = m_texUniformLoc;
    cmd.vertexByteOff = work.uniformPool.offsetOf(vertDst);
    cmd.vertexCount   = 6;
    cmd.mvpByteOff    = work.uniformPool.offsetOf(mvpDst);
    cmd._pad0         = 0;
    cmd.poolBase      = poolBase;

    work.renderCommands.push_back(
        RenderCommand::create(cmd, &Graphics::execDrawTexture, sortKey));
}

void Graphics::execDrawBatch(const void* payload) noexcept {
    const auto& cmd = *static_cast<const DrawBatchCmd*>(payload);

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

    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    cmd.vertexCount * static_cast<GLsizeiptr>(sizeof(VertexPosColor)),
                    verts);

    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(cmd.vertexCount));
}

void Graphics::execDrawTexture(const void* payload) noexcept {
    const auto& cmd = *static_cast<const DrawTextureCmd*>(payload);

    const auto* verts = reinterpret_cast<const VertexPosColorTex*>(
                            cmd.poolBase + cmd.vertexByteOff);
    const auto* mvp   = reinterpret_cast<const float*>(
                            cmd.poolBase + cmd.mvpByteOff);

    glUseProgram(cmd.program);

    if (cmd.mvpLoc != -1) {
        glUniformMatrix4fv(cmd.mvpLoc, 1, GL_FALSE, mvp);
    }
    if (cmd.texLoc != -1) {
        glUniform1i(cmd.texLoc, 0);
    }

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, cmd.texture);

    glBindVertexArray(cmd.vao);
    glBindBuffer(GL_ARRAY_BUFFER, cmd.vbo);

    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    cmd.vertexCount * static_cast<GLsizeiptr>(sizeof(VertexPosColorTex)),
                    verts);

    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(cmd.vertexCount));
}

} // namespace spt
