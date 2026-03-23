#pragma once

#include "../glad/glad.h"
#include "Shader.h"
#include "../core/GameWork.h"
#include <memory>

namespace spt {

struct VertexPosColor {
    float x, y;
    float r, g, b, a;
};

class Graphics {
public:
    Graphics() = default;
    ~Graphics();

    Graphics(const Graphics&) = delete;
    Graphics& operator=(const Graphics&) = delete;

    bool initialize();
    void shutdown();

    void clear(float r, float g, float b, float a);
    void setViewport(int x, int y, int width, int height);

    void drawTriangle(float x1, float y1, float r1, float g1, float b1,
                      float x2, float y2, float r2, float g2, float b2,
                      float x3, float y3, float r3, float g3, float b3);

    void flush();

    void recordDrawTriangle(GameWork& work,
                            float x1, float y1, float r1, float g1, float b1,
                            float x2, float y2, float r2, float g2, float b2,
                            float x3, float y3, float r3, float g3, float b3);

    GLuint vao() const { return m_vao; }
    GLuint program() const;

private:
    bool createDefaultResources();

    GLuint m_vao = 0;
    GLuint m_vbo = 0;
    std::unique_ptr<Shader> m_basicShader;

    static const int MAX_VERTICES = 1024;
    VertexPosColor m_vertexBuffer[MAX_VERTICES];
    int m_vertexCount = 0;

    float m_mvp[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
    bool m_errorPrinted = false;
};

}
