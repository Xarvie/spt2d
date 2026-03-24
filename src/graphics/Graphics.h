#pragma once

#include "../glad/glad.h"
#include "Shader.h"
#include "GameWork.h"

#include <memory>
#include <cstdint>
#include <type_traits>

namespace spt {

struct VertexPosColor {
    float x, y;
    float r, g, b, a;
};
static_assert(std::is_trivially_copyable<VertexPosColor>::value);

struct VertexPosColorTex {
    float x, y;
    float u, v;
    float r, g, b, a;
};
static_assert(std::is_trivially_copyable<VertexPosColorTex>::value);


struct DrawBatchCmd {
    GLuint          program;
    GLuint          vao;
    GLuint          vbo;
    GLint           mvpLoc;
    uint32_t        vertexByteOff;
    uint32_t        vertexCount;
    uint32_t        mvpByteOff;
    uint32_t        _pad0;
    const uint8_t*  poolBase;
};
static_assert(sizeof(DrawBatchCmd) <= RenderCommand::kPayloadSize,
              "DrawBatchCmd exceeds RenderCommand payload capacity");
static_assert(std::is_trivially_copyable<DrawBatchCmd>::value,
              "DrawBatchCmd must be trivially copyable");


struct DrawTextureCmd {
    GLuint          program;
    GLuint          vao;
    GLuint          vbo;
    GLuint          texture;
    GLint           mvpLoc;
    GLint           texLoc;
    uint32_t        vertexByteOff;
    uint32_t        vertexCount;
    uint32_t        mvpByteOff;
    uint32_t        _pad0;
    const uint8_t*  poolBase;
};
static_assert(sizeof(DrawTextureCmd) <= RenderCommand::kPayloadSize,
              "DrawTextureCmd exceeds RenderCommand payload capacity");
static_assert(std::is_trivially_copyable<DrawTextureCmd>::value,
              "DrawTextureCmd must be trivially copyable");


class Graphics {
public:
    static constexpr int kMaxImmediateVertices = 4096;
    static constexpr int kMaxTextureVertices = 4096;

    static const float kIdentityMVP[16];

    Graphics()  = default;
    ~Graphics();

    Graphics(const Graphics&)            = delete;
    Graphics& operator=(const Graphics&) = delete;

    bool initialize();
    void shutdown();

    [[nodiscard]] bool isInitialized() const noexcept { return m_initialized; }

    void setMVP(const float* mvp16) noexcept;

    void clear(float r, float g, float b, float a);

    void setViewport(int x, int y, int width, int height);

    void drawTriangle(float x1, float y1, float r1, float g1, float b1,
                      float x2, float y2, float r2, float g2, float b2,
                      float x3, float y3, float r3, float g3, float b3);

    void drawTexture(GLuint texture, float x, float y, float w, float h,
                     float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 1.0f);

    void drawTextureRect(GLuint texture, float x, float y, float w, float h,
                         float u0, float v0, float u1, float v1,
                         float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 1.0f);

    void flush();
    void flushTextures();

    void recordTriangles(GameWork&             work,
                         const VertexPosColor* verts,
                         int                   count,
                         const float*          mvp16   = nullptr,
                         uint64_t              sortKey = 0);

    void recordTriangle(GameWork& work,
                        float x1, float y1, float r1, float g1, float b1,
                        float x2, float y2, float r2, float g2, float b2,
                        float x3, float y3, float r3, float g3, float b3,
                        const float* mvp16  = nullptr,
                        uint64_t     sortKey = 0);

    void recordTexture(GameWork& work, GLuint texture,
                       float x, float y, float w, float h,
                       const float* mvp16 = nullptr,
                       uint64_t sortKey = 0);

    [[nodiscard]] GLuint vao()     const noexcept { return m_vao; }
    [[nodiscard]] GLuint program() const noexcept {
        return m_basicShader ? m_basicShader->program() : 0;
    }
    [[nodiscard]] GLuint textureProgram() const noexcept {
        return m_textureShader ? m_textureShader->program() : 0;
    }

private:
    bool createGLResources();
    bool createTextureGLResources();

    static void execDrawBatch(const void* payload) noexcept;
    static void execDrawTexture(const void* payload) noexcept;

    GLuint                  m_vao = 0;
    GLuint                  m_vbo = 0;
    std::unique_ptr<Shader> m_basicShader;

    GLuint                  m_texVao = 0;
    GLuint                  m_texVbo = 0;
    std::unique_ptr<Shader> m_textureShader;

    GLint m_mvpUniformLoc = -1;
    GLint m_texMvpUniformLoc = -1;
    GLint m_texUniformLoc = -1;

    VertexPosColor m_immBuffer[kMaxImmediateVertices];
    int            m_immCount = 0;

    VertexPosColorTex m_texImmBuffer[kMaxTextureVertices];
    int               m_texImmCount = 0;
    GLuint            m_currentTexture = 0;

    float m_mvp[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };

    bool m_initialized = false;
};

} // namespace spt
