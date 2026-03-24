#include "Canvas.h"
#include <iostream>
#include <algorithm>
#include <cmath>

namespace spt3d {

Canvas::Canvas() {
    Mat3::identity(m_matrix);
    m_tint = Color::white();
    m_currentClip = {0, 0, 0, 0, false};
}

Canvas::~Canvas() {
    shutdown();
}

bool Canvas::initialize(Graphics* graphics) {
    if (!graphics) return false;
    m_graphics = graphics;
    m_initialized = true;
    return true;
}

void Canvas::shutdown() {
    m_matrixStack.clear();
    m_clipStack.clear();
    m_batches.clear();
    m_currentBatch = nullptr;
    m_graphics = nullptr;
    m_initialized = false;
}

void Canvas::beginFrame(int width, int height) {
    m_width = width;
    m_height = height;

    Mat3::identity(m_matrix);
    m_matrixStack.clear();
    m_clipStack.clear();
    m_currentClip = {0, 0, static_cast<float>(width), static_cast<float>(height), true};

    m_tint = Color::white();
    m_blendMode = BlendMode::Alpha;
    m_sortKey = 0;

    m_batches.clear();
    m_currentBatch = nullptr;

    float ortho[9];
    Mat3::ortho(ortho, 0, static_cast<float>(width), static_cast<float>(height), 0);
    Mat3::toMat4(ortho, m_projection);

    m_inFrame = true;
}

void Canvas::endFrame() {
    flush();
    m_inFrame = false;
}

void Canvas::pushMatrix() {
    MatrixState state;
    Mat3::copy(state.matrix, m_matrix);
    m_matrixStack.push_back(state);
}

void Canvas::popMatrix() {
    if (m_matrixStack.empty()) return;
    MatrixState& state = m_matrixStack.back();
    Mat3::copy(m_matrix, state.matrix);
    m_matrixStack.pop_back();
}

void Canvas::translate(float x, float y) {
    Mat3::translate(m_matrix, x, y);
}

void Canvas::rotate(float radians) {
    Mat3::rotate(m_matrix, radians);
}

void Canvas::scale(float sx, float sy) {
    Mat3::scale(m_matrix, sx, sy);
}

void Canvas::pushClipRect(float x, float y, float w, float h) {
    ClipState state = m_currentClip;
    m_clipStack.push_back(state);

    float newX = x;
    float newY = y;
    float newW = w;
    float newH = h;

    if (m_currentClip.enabled) {
        newX = std::max(x, m_currentClip.x);
        newY = std::max(y, m_currentClip.y);
        float right = std::min(x + w, m_currentClip.x + m_currentClip.w);
        float bottom = std::min(y + h, m_currentClip.y + m_currentClip.h);
        newW = std::max(0.0f, right - newX);
        newH = std::max(0.0f, bottom - newY);
    }

    m_currentClip = {newX, newY, newW, newH, true};
}

void Canvas::popClipRect() {
    if (m_clipStack.empty()) return;
    m_currentClip = m_clipStack.back();
    m_clipStack.pop_back();
}

void Canvas::setTint(const Color& color) {
    m_tint = color;
}

void Canvas::setBlendMode(BlendMode mode) {
    if (m_blendMode != mode) {
        flush();
        m_blendMode = mode;
    }
}

void Canvas::setSortKey(uint64_t key) {
    m_sortKey = key;
}

void Canvas::drawTexture(GLuint texture, float x, float y) {
    drawTexture(texture, x, y, 0, 0);
}

void Canvas::drawTexture(GLuint texture, float x, float y, float w, float h) {
    if (w == 0 || h == 0) {
        w = static_cast<float>(m_width);
        h = static_cast<float>(m_height);
    }
    drawTextureRect(texture, x, y, w, h, 0, 0, 1, 1);
}

void Canvas::drawTextureRect(GLuint texture, float x, float y, float w, float h,
                              float u0, float v0, float u1, float v1) {
    float x0 = x, y0 = y;
    float x1 = x + w, y1 = y;
    float x2 = x + w, y2 = y + h;
    float x3 = x, y3 = y + h;

    transformQuad(x0, y0, x1, y1, x2, y2, x3, y3);

    if (!clipQuad(x0, y0, x1, y1, x2, y2, x3, y3, u0, v0, u1, v1)) {
        return;
    }

    if (!m_currentBatch || m_currentBatch->texture != texture ||
        m_currentBatch->vertices.size() + 6 > kMaxBatchVertices) {
        flush();
        m_batches.emplace_back();
        m_currentBatch = &m_batches.back();
        m_currentBatch->texture = texture;
        m_currentBatch->blendMode = m_blendMode;
    }

    addQuad(*m_currentBatch,
            x0, y0, x1, y1, x2, y2, x3, y3,
            u0, v0, u1, v1,
            m_tint.r, m_tint.g, m_tint.b, m_tint.a);
}

void Canvas::drawTextureRotated(GLuint texture, float x, float y, float w, float h,
                                  float rotation, float pivotX, float pivotY) {
    pushMatrix();
    translate(x + w * pivotX, y + h * pivotY);
    rotate(rotation);
    translate(-w * pivotX, -h * pivotY);
    drawTexture(texture, 0, 0, w, h);
    popMatrix();
}

void Canvas::drawRect(float x, float y, float w, float h, const Color& color, bool filled) {
    Color oldTint = m_tint;
    m_tint = color;

    if (filled) {
        drawTextureRect(0, x, y, w, h, 0, 0, 1, 1);
    } else {
        drawLine(x, y, x + w, y, color);
        drawLine(x + w, y, x + w, y + h, color);
        drawLine(x + w, y + h, x, y + h, color);
        drawLine(x, y + h, x, y, color);
    }

    m_tint = oldTint;
}

void Canvas::drawLine(float x1, float y1, float x2, float y2, const Color& color, float thickness) {
    float dx = x2 - x1;
    float dy = y2 - y1;
    float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.001f) return;

    float nx = -dy / len * thickness * 0.5f;
    float ny = dx / len * thickness * 0.5f;

    float px0 = x1 + nx, py0 = y1 + ny;
    float px1 = x1 - nx, py1 = y1 - ny;
    float px2 = x2 - nx, py2 = y2 - ny;
    float px3 = x2 + nx, py3 = y2 + ny;

    transformQuad(px0, py0, px1, py1, px2, py2, px3, py3);

    float u0 = 0, v0 = 0, u1 = 1, v1 = 1;

    if (!m_currentBatch || m_currentBatch->vertices.size() + 6 > kMaxBatchVertices) {
        flush();
        m_batches.emplace_back();
        m_currentBatch = &m_batches.back();
        m_currentBatch->texture = 0;
        m_currentBatch->blendMode = m_blendMode;
    }

    addQuad(*m_currentBatch,
            px0, py0, px1, py1, px2, py2, px3, py3,
            u0, v0, u1, v1,
            color.r, color.g, color.b, color.a);
}

void Canvas::flush() {
    for (auto& batch : m_batches) {
        if (!batch.vertices.empty()) {
            flushBatch(batch);
        }
    }
    m_batches.clear();
    m_currentBatch = nullptr;
}

void Canvas::flushBatch(Batch& batch) {
    if (batch.vertices.empty() || !m_graphics) return;

    applyBlendMode(batch.blendMode);
    m_graphics->setMVP(m_projection);

    for (size_t i = 0; i < batch.vertices.size(); i += 6) {
        const BatchVertex* v = &batch.vertices[i];

        m_graphics->drawTextureRect(batch.texture,
                                     v[0].x, v[0].y, v[2].x - v[0].x, v[2].y - v[0].y,
                                     v[0].u, v[0].v, v[2].u, v[2].v,
                                     v[0].r, v[0].g, v[0].b, v[0].a);
    }
}

void Canvas::addQuad(Batch& batch,
                      float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3,
                      float u0, float v0, float u1, float v1,
                      float r, float g, float b, float a) {
    batch.vertices.push_back({x0, y0, u0, v0, r, g, b, a});
    batch.vertices.push_back({x1, y1, u1, v0, r, g, b, a});
    batch.vertices.push_back({x2, y2, u1, v1, r, g, b, a});
    batch.vertices.push_back({x0, y0, u0, v0, r, g, b, a});
    batch.vertices.push_back({x2, y2, u1, v1, r, g, b, a});
    batch.vertices.push_back({x3, y3, u0, v1, r, g, b, a});
}

void Canvas::transformQuad(float& x0, float& y0, float& x1, float& y1,
                            float& x2, float& y2, float& x3, float& y3) {
    Mat3::transformPoint(m_matrix, x0, y0, &x0, &y0);
    Mat3::transformPoint(m_matrix, x1, y1, &x1, &y1);
    Mat3::transformPoint(m_matrix, x2, y2, &x2, &y2);
    Mat3::transformPoint(m_matrix, x3, y3, &x3, &y3);
}

bool Canvas::clipQuad(float& x0, float& y0, float& x1, float& y1,
                       float& x2, float& y2, float& x3, float& y3,
                       float&, float&, float&, float&) {
    if (!m_currentClip.enabled) return true;
    if (m_currentClip.w <= 0 || m_currentClip.h <= 0) return false;

    float minX = m_currentClip.x;
    float minY = m_currentClip.y;
    float maxX = m_currentClip.x + m_currentClip.w;
    float maxY = m_currentClip.y + m_currentClip.h;

    x0 = std::max(minX, std::min(maxX, x0));
    y0 = std::max(minY, std::min(maxY, y0));
    x1 = std::max(minX, std::min(maxX, x1));
    y1 = std::max(minY, std::min(maxY, y1));
    x2 = std::max(minX, std::min(maxX, x2));
    y2 = std::max(minY, std::min(maxY, y2));
    x3 = std::max(minX, std::min(maxX, x3));
    y3 = std::max(minY, std::min(maxY, y3));

    return true;
}

}
