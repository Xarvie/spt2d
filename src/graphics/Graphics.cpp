#include "Graphics.h"
#include "ShaderSources.h"
#include <iostream>
#include <cstring>

namespace spt {

Graphics::~Graphics() {
    shutdown();
}

bool Graphics::initialize() {
    if (!createDefaultResources()) {
        std::cerr << "[Graphics] Failed to create default resources" << std::endl;
        return false;
    }
    return true;
}

void Graphics::shutdown() {
    if (m_vbo != 0) {
        glDeleteBuffers(1, &m_vbo);
        m_vbo = 0;
    }
    if (m_vao != 0) {
        glDeleteVertexArrays(1, &m_vao);
        m_vao = 0;
    }
    m_basicShader.reset();
    m_vertexCount = 0;
}

bool Graphics::createDefaultResources() {
    m_basicShader = std::make_unique<Shader>();
    if (!m_basicShader->loadFromSource(SHADER_VS_BASIC, SHADER_FS_BASIC)) {
        std::cerr << "[Graphics] Failed to load basic shader" << std::endl;
        return false;
    }

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(m_vertexBuffer), nullptr, GL_DYNAMIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(VertexPosColor),
                          reinterpret_cast<void*>(offsetof(VertexPosColor, x)));

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(VertexPosColor),
                          reinterpret_cast<void*>(offsetof(VertexPosColor, r)));

    glBindVertexArray(0);

    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        std::cerr << "[Graphics] GL error after setup: 0x" << std::hex << err << std::dec << std::endl;
        return false;
    }

    return true;
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
    if (m_vertexCount + 3 > MAX_VERTICES) {
        flush();
    }

    VertexPosColor* v = &m_vertexBuffer[m_vertexCount];
    v[0] = {x1, y1, r1, g1, b1, 1.0f};
    v[1] = {x2, y2, r2, g2, b2, 1.0f};
    v[2] = {x3, y3, r3, g3, b3, 1.0f};
    m_vertexCount += 3;
}

void Graphics::flush() {
    if (m_vertexCount == 0) return;

    m_basicShader->use();
    m_basicShader->setMat4("u_mvp", m_mvp);

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, m_vertexCount * sizeof(VertexPosColor), m_vertexBuffer);

    glDrawArrays(GL_TRIANGLES, 0, m_vertexCount);

    m_vertexCount = 0;
}

}
