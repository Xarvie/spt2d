// spt3d/resource/MaterialSnapshot.h — Immutable material snapshot
// [THREAD: logic→build, render→read]
//
// A MaterialSnapshot is a VALUE TYPE that captures all material state at a
// point in time. The logic thread builds it, copies it into GameWork (via
// uniformPool), and the render thread reads the copy. No shared mutable state.
//
// Unlike the old Material (shared_ptr<Impl> with mutable uniforms), this
// design eliminates cross-thread data races by construction.
//
// Usage on logic thread:
//   MaterialSnapshot snap;
//   snap.shader = myShaderHandle;
//   snap.state.blend = BlendMode::Alpha;
//   snap.setFloat("u_roughness", 0.5f);
//   snap.setTexture("u_albedo", myTexHandle, 0);
//   // ... then store into DrawItem or allocUniform into GameWork
#pragma once

#include "../Handle.h"
#include "../Types.h"

#include <cstdint>
#include <cstring>

namespace spt3d {

// =========================================================================
//  Compile-time string hash (FNV-1a 32-bit)
//  Used to map uniform names to fixed-size arrays without heap allocation.
// =========================================================================

constexpr int32_t HashName(const char* s) noexcept {
    uint32_t h = 2166136261u;
    while (*s) { h ^= static_cast<uint32_t>(*s++); h *= 16777619u; }
    return static_cast<int32_t>(h);
}

inline int32_t HashName(std::string_view s) noexcept {
    uint32_t h = 2166136261u;
    for (char c : s) { h ^= static_cast<uint32_t>(c); h *= 16777619u; }
    return static_cast<int32_t>(h);
}

// =========================================================================
//  MaterialSnapshot
// =========================================================================

struct MaterialSnapshot {
    static constexpr int kMaxTextures = 8;
    static constexpr int kMaxFloats   = 32;
    static constexpr int kMaxVec4s    = 16;
    static constexpr int kMaxMat4s    = 4;

    // ── Shader + state ──────────────────────────────────────────────────
    ShaderHandle shader;
    RenderState  state;
    uint32_t     tagHash = 0;       // hashed tag for filtering

    // ── Texture bindings ────────────────────────────────────────────────
    struct TexBinding {
        TexHandle tex;
        int32_t   nameHash = 0;
        int       slot     = -1;
    };
    TexBinding textures[kMaxTextures] = {};
    int        texCount = 0;

    // ── Scalar float uniforms ───────────────────────────────────────────
    struct FloatEntry {
        int32_t nameHash = 0;
        float   value    = 0;
    };
    FloatEntry floats[kMaxFloats] = {};
    int        floatCount = 0;

    // ── Vec4 uniforms (also used for Vec2/Vec3/Color) ───────────────────
    struct Vec4Entry {
        int32_t nameHash = 0;
        Vec4    value{0};
    };
    Vec4Entry vec4s[kMaxVec4s] = {};
    int       vec4Count = 0;

    // ── Mat4 uniforms ───────────────────────────────────────────────────
    struct Mat4Entry {
        int32_t nameHash = 0;
        Mat4    value{1.0f};
    };
    Mat4Entry mat4s[kMaxMat4s] = {};
    int       mat4Count = 0;

    // ── Setters (logic thread) ──────────────────────────────────────────

    void setTexture(std::string_view name, TexHandle tex, int slot) {
        if (texCount >= kMaxTextures) return;
        textures[texCount++] = {tex, HashName(name), slot};
    }

    void setFloat(std::string_view name, float v) {
        const int32_t h = HashName(name);
        for (int i = 0; i < floatCount; ++i)
            if (floats[i].nameHash == h) { floats[i].value = v; return; }
        if (floatCount >= kMaxFloats) return;
        floats[floatCount++] = {h, v};
    }

    void setVec2(std::string_view name, Vec2 v) {
        setVec4(name, Vec4(v, 0, 0));
    }

    void setVec3(std::string_view name, Vec3 v) {
        setVec4(name, Vec4(v, 0));
    }

    void setVec4(std::string_view name, Vec4 v) {
        const int32_t h = HashName(name);
        for (int i = 0; i < vec4Count; ++i)
            if (vec4s[i].nameHash == h) { vec4s[i].value = v; return; }
        if (vec4Count >= kMaxVec4s) return;
        vec4s[vec4Count++] = {h, v};
    }

    void setColor(std::string_view name, Color c) {
        setVec4(name, ToVec4(c));
    }

    void setMat4(std::string_view name, Mat4 m) {
        const int32_t h = HashName(name);
        for (int i = 0; i < mat4Count; ++i)
            if (mat4s[i].nameHash == h) { mat4s[i].value = m; return; }
        if (mat4Count >= kMaxMat4s) return;
        mat4s[mat4Count++] = {h, m};
    }

    void setTag(std::string_view tag) {
        tagHash = static_cast<uint32_t>(HashName(tag));
    }

    // ── PBR convenience setters ─────────────────────────────────────────

    void setAlbedo(TexHandle tex)           { setTexture("u_albedo", tex, 0); }
    void setAlbedoColor(Color c)            { setColor("u_albedoColor", c); }
    void setNormalMap(TexHandle tex)         { setTexture("u_normalMap", tex, 1); }
    void setMetallicRoughness(TexHandle tex) { setTexture("u_metallicRoughness", tex, 3); }
    void setMetallic(float v)               { setFloat("u_metallic", v); }
    void setRoughness(float v)              { setFloat("u_roughness", v); }
    void setEmission(TexHandle tex)         { setTexture("u_emission", tex, 2); }
    void setEmissionStrength(float v)       { setFloat("u_emissionStrength", v); }
};

} // namespace spt3d
