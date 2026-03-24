#pragma once

#include <cmath>
#include <cstring>

namespace spt3d {

namespace Mat3 {

constexpr float PI = 3.14159265358979323846f;

inline void identity(float* out) {
    out[0] = 1; out[3] = 0; out[6] = 0;
    out[1] = 0; out[4] = 1; out[7] = 0;
    out[2] = 0; out[5] = 0; out[8] = 1;
}

inline void copy(float* dst, const float* src) {
    std::memcpy(dst, src, 9 * sizeof(float));
}

inline void multiply(const float* a, const float* b, float* out) {
    float temp[9];
    temp[0] = a[0]*b[0] + a[3]*b[1] + a[6]*b[2];
    temp[1] = a[1]*b[0] + a[4]*b[1] + a[7]*b[2];
    temp[2] = a[2]*b[0] + a[5]*b[1] + a[8]*b[2];

    temp[3] = a[0]*b[3] + a[3]*b[4] + a[6]*b[5];
    temp[4] = a[1]*b[3] + a[4]*b[4] + a[7]*b[5];
    temp[5] = a[2]*b[3] + a[5]*b[4] + a[8]*b[5];

    temp[6] = a[0]*b[6] + a[3]*b[7] + a[6]*b[8];
    temp[7] = a[1]*b[6] + a[4]*b[7] + a[7]*b[8];
    temp[8] = a[2]*b[6] + a[5]*b[7] + a[8]*b[8];

    std::memcpy(out, temp, 9 * sizeof(float));
}

inline void premultiply(const float* a, const float* b, float* out) {
    multiply(b, a, out);
}

inline void translate(float* m, float tx, float ty) {
    m[6] += m[0] * tx + m[3] * ty;
    m[7] += m[1] * tx + m[4] * ty;
}

inline void rotate(float* m, float radians) {
    float c = std::cos(radians);
    float s = std::sin(radians);

    float m0 = m[0], m1 = m[1], m3 = m[3], m4 = m[4];

    m[0] = m0 * c + m3 * s;
    m[1] = m1 * c + m4 * s;
    m[3] = m0 * -s + m3 * c;
    m[4] = m1 * -s + m4 * c;
}

inline void scale(float* m, float sx, float sy) {
    m[0] *= sx; m[3] *= sy;
    m[1] *= sx; m[4] *= sy;
}

inline void ortho(float* out, float left, float right, float bottom, float top) {
    identity(out);
    out[0] = 2.0f / (right - left);
    out[4] = 2.0f / (top - bottom);
    out[6] = -(right + left) / (right - left);
    out[7] = -(top + bottom) / (top - bottom);
}

inline void transformPoint(const float* m, float x, float y, float* outX, float* outY) {
    *outX = m[0] * x + m[3] * y + m[6];
    *outY = m[1] * x + m[4] * y + m[7];
}

inline void toMat4(const float* m3, float* m4) {
    m4[0]  = m3[0]; m4[1]  = m3[1]; m4[2]  = 0;    m4[3]  = 0;
    m4[4]  = m3[3]; m4[5]  = m3[4]; m4[6]  = 0;    m4[7]  = 0;
    m4[8]  = 0;     m4[9]  = 0;     m4[10] = 1;    m4[11] = 0;
    m4[12] = m3[6]; m4[13] = m3[7]; m4[14] = 0;    m4[15] = m3[8];
}

inline float degToRad(float degrees) {
    return degrees * PI / 180.0f;
}

inline float radToDeg(float radians) {
    return radians * 180.0f / PI;
}

inline void invert(const float* m, float* out) {
    float det = m[0] * (m[4] * m[8] - m[7] * m[5]) -
                m[3] * (m[1] * m[8] - m[7] * m[2]) +
                m[6] * (m[1] * m[5] - m[4] * m[2]);

    if (det == 0) {
        identity(out);
        return;
    }

    det = 1.0f / det;

    out[0] = (m[4] * m[8] - m[7] * m[5]) * det;
    out[1] = (m[7] * m[2] - m[1] * m[8]) * det;
    out[2] = (m[1] * m[5] - m[4] * m[2]) * det;

    out[3] = (m[6] * m[5] - m[3] * m[8]) * det;
    out[4] = (m[0] * m[8] - m[6] * m[2]) * det;
    out[5] = (m[3] * m[2] - m[0] * m[5]) * det;

    out[6] = (m[3] * m[7] - m[6] * m[4]) * det;
    out[7] = (m[6] * m[1] - m[0] * m[7]) * det;
    out[8] = (m[0] * m[4] - m[3] * m[1]) * det;
}

}

}
