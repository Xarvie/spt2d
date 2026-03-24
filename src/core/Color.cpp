#include "../spt3d.h"
#include <cmath>
#include <algorithm>

namespace spt3d {

Vec4 ToVec4(Color c) {
    return Vec4(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f);
}

Color FromVec4(Vec4 v) {
    return Color{
        static_cast<uint8_t>(std::clamp(v.r * 255.0f, 0.0f, 255.0f)),
        static_cast<uint8_t>(std::clamp(v.g * 255.0f, 0.0f, 255.0f)),
        static_cast<uint8_t>(std::clamp(v.b * 255.0f, 0.0f, 255.0f)),
        static_cast<uint8_t>(std::clamp(v.a * 255.0f, 0.0f, 255.0f))
    };
}

Color FromHSV(float h, float s, float v) {
    h = std::fmod(h, 360.0f);
    if (h < 0) h += 360.0f;
    
    s = std::clamp(s, 0.0f, 1.0f);
    v = std::clamp(v, 0.0f, 1.0f);
    
    float c = v * s;
    float x = c * (1.0f - std::abs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
    float m = v - c;
    
    float r, g, b;
    if (h < 60)       { r = c; g = x; b = 0; }
    else if (h < 120) { r = x; g = c; b = 0; }
    else if (h < 180) { r = 0; g = c; b = x; }
    else if (h < 240) { r = 0; g = x; b = c; }
    else if (h < 300) { r = x; g = 0; b = c; }
    else              { r = c; g = 0; b = x; }
    
    return Color{
        static_cast<uint8_t>((r + m) * 255.0f),
        static_cast<uint8_t>((g + m) * 255.0f),
        static_cast<uint8_t>((b + m) * 255.0f),
        255
    };
}

Color FromHex(uint32_t hex) {
    return Color{
        static_cast<uint8_t>((hex >> 24) & 0xFF),
        static_cast<uint8_t>((hex >> 16) & 0xFF),
        static_cast<uint8_t>((hex >> 8) & 0xFF),
        static_cast<uint8_t>(hex & 0xFF)
    };
}

Color CLerp(Color a, Color b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return Color{
        static_cast<uint8_t>(a.r + (b.r - a.r) * t),
        static_cast<uint8_t>(a.g + (b.g - a.g) * t),
        static_cast<uint8_t>(a.b + (b.b - a.b) * t),
        static_cast<uint8_t>(a.a + (b.a - a.a) * t)
    };
}

Color CAlpha(Color c, float alpha) {
    return Color{ c.r, c.g, c.b, static_cast<uint8_t>(std::clamp(alpha * 255.0f, 0.0f, 255.0f)) };
}

}
