#pragma once

#include <cstdint>

namespace spt {

struct Color {
    float r, g, b, a;

    Color() : r(1), g(1), b(1), a(1) {}
    Color(float r, float g, float b, float a = 1.0f) : r(r), g(g), b(b), a(a) {}

    static Color white()   { return Color(1, 1, 1, 1); }
    static Color black()   { return Color(0, 0, 0, 1); }
    static Color red()     { return Color(1, 0, 0, 1); }
    static Color green()   { return Color(0, 1, 0, 1); }
    static Color blue()    { return Color(0, 0, 1, 1); }
    static Color yellow()  { return Color(1, 1, 0, 1); }
    static Color cyan()    { return Color(0, 1, 1, 1); }
    static Color magenta() { return Color(1, 0, 1, 1); }
    static Color transparent() { return Color(0, 0, 0, 0); }
    static Color gray(float v) { return Color(v, v, v, 1); }

    static Color fromRGBA(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
        return Color(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
    }

    static Color fromHex(uint32_t hex) {
        return Color(
            ((hex >> 24) & 0xFF) / 255.0f,
            ((hex >> 16) & 0xFF) / 255.0f,
            ((hex >> 8) & 0xFF) / 255.0f,
            (hex & 0xFF) / 255.0f
        );
    }

    Color withAlpha(float newAlpha) const {
        return Color(r, g, b, newAlpha);
    }

    Color operator*(float s) const {
        return Color(r * s, g * s, b * s, a * s);
    }

    Color operator*(const Color& c) const {
        return Color(r * c.r, g * c.g, b * c.b, a * c.a);
    }

    Color operator+(const Color& c) const {
        return Color(r + c.r, g + c.g, b + c.b, a + c.a);
    }

    Color lerp(const Color& to, float t) const {
        return Color(
            r + (to.r - r) * t,
            g + (to.g - g) * t,
            b + (to.b - b) * t,
            a + (to.a - a) * t
        );
    }
};

}
