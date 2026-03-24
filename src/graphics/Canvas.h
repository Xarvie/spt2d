#pragma once

#include "Graphics.h"
#include "Color.h"
#include "BlendMode.h"
#include "Mat3.h"

#include <vector>

namespace spt {

class Canvas {
public:
    static constexpr int kMaxBatchVertices = 8192;

    Canvas();
    ~Canvas();

    Canvas(const Canvas&) = delete;
    Canvas& operator=(const Canvas&) = delete;

    bool initialize(Graphics* graphics);
    void shutdown();

    void beginFrame(int width, int height);
    void endFrame();

    void pushMatrix();
    void popMatrix();
    void translate(float x, float y);
    void rotate(float radians);
    void scale(float sx, float sy);

    void pushClipRect(float x, float y, float w, float h);
    void popClipRect();

    void setTint(const Color& color);
    void setBlendMode(BlendMode mode);
    void setSortKey(uint64_t key);

    void drawTexture(GLuint texture, float x, float y);
    void drawTexture(GLuint texture, float x, float y, float w, float h);
    void drawTextureRect(GLuint texture, float x, float y, float w, float h,
                         float u0, float v0, float u1, float v1);
    void drawTextureRotated(GLuint texture, float x, float y, float w, float h,
                            float rotation, float pivotX = 0.5f, float pivotY = 0.5f);

    void drawRect(float x, float y, float w, float h, const Color& color, bool filled = true);
    void drawLine(float x1, float y1, float x2, float y2, const Color& color, float thickness = 1.0f);

    void flush();

    int getWidth() const { return m_width; }
    int getHeight() const { return m_height; }

private:
    struct MatrixState {
        float matrix[9];
    };

    struct ClipState {
        float x, y, w, h;
        bool enabled;
    };

    struct BatchVertex {
        float x, y;
        float u, v;
        float r, g, b, a;
    };

    struct Batch {
        GLuint texture = 0;
        BlendMode blendMode = BlendMode::Alpha;
        std::vector<BatchVertex> vertices;
    };

    void flushBatch(Batch& batch);
    void addQuad(Batch& batch,
                 float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3,
                 float u0, float v0, float u1, float v1,
                 float r, float g, float b, float a);

    void transformQuad(float& x0, float& y0, float& x1, float& y1,
                       float& x2, float& y2, float& x3, float& y3);

    bool clipQuad(float& x0, float& y0, float& x1, float& y1,
                  float& x2, float& y2, float& x3, float& y3,
                  float& u0, float& v0, float& u1, float& v1);

    Graphics* m_graphics = nullptr;

    std::vector<MatrixState> m_matrixStack;
    float m_matrix[9];

    std::vector<ClipState> m_clipStack;
    ClipState m_currentClip;

    Color m_tint;
    BlendMode m_blendMode = BlendMode::Alpha;
    uint64_t m_sortKey = 0;

    std::vector<Batch> m_batches;
    Batch* m_currentBatch = nullptr;

    int m_width = 0;
    int m_height = 0;

    float m_projection[16];

    bool m_initialized = false;
    bool m_inFrame = false;
};

}
