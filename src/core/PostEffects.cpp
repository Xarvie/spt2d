#include "../Spt3D.h"
#include "../glad/glad.h"

#include <vector>

namespace spt3d {

static const char* s_blitVS = R"(#version 300 es
precision highp float;
layout(location = 0) in vec3 a_position;
layout(location = 2) in vec2 a_texcoord;
out vec2 v_uv;
void main() {
    gl_Position = vec4(a_position.xy, 0.0, 1.0);
    v_uv = a_texcoord;
}
)";

static const char* s_blitFS = R"(#version 300 es
precision highp float;
in vec2 v_uv;
uniform sampler2D u_texture;
out vec4 fragColor;
void main() {
    fragColor = texture(u_texture, v_uv);
}
)";

static const char* s_bloomBrightFS = R"(#version 300 es
precision highp float;
in vec2 v_uv;
uniform sampler2D u_texture;
uniform float u_threshold;
out vec4 fragColor;
void main() {
    vec4 color = texture(u_texture, v_uv);
    float brightness = dot(color.rgb, vec3(0.2126, 0.7152, 0.0722));
    fragColor = brightness > u_threshold ? color : vec4(0.0);
}
)";

static const char* s_bloomBlurFS = R"(#version 300 es
precision highp float;
in vec2 v_uv;
uniform sampler2D u_texture;
uniform vec2 u_direction;
out vec4 fragColor;
void main() {
    vec4 color = vec4(0.0);
    vec2 texelSize = 1.0 / vec2(textureSize(u_texture, 0));
    float weights[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);
    for (int i = -4; i <= 4; i++) {
        color += texture(u_texture, v_uv + float(i) * u_direction * texelSize) * weights[abs(i)];
    }
    fragColor = color;
}
)";

static const char* s_bloomCombineFS = R"(#version 300 es
precision highp float;
in vec2 v_uv;
uniform sampler2D u_texture;
uniform sampler2D u_bloom;
uniform float u_intensity;
out vec4 fragColor;
void main() {
    vec4 color = texture(u_texture, v_uv);
    vec4 bloom = texture(u_bloom, v_uv);
    fragColor = color + bloom * u_intensity;
}
)";

static const char* s_toneMapACESFS = R"(#version 300 es
precision highp float;
in vec2 v_uv;
uniform sampler2D u_texture;
out vec4 fragColor;
void main() {
    vec4 color = texture(u_texture, v_uv);
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    vec3 mapped = clamp((color.rgb * (a * color.rgb + b)) / (color.rgb * (c * color.rgb + d) + e), 0.0, 1.0);
    fragColor = vec4(mapped, color.a);
}
)";

static const char* s_toneMapReinhardFS = R"(#version 300 es
precision highp float;
in vec2 v_uv;
uniform sampler2D u_texture;
out vec4 fragColor;
void main() {
    vec4 color = texture(u_texture, v_uv);
    vec3 mapped = color.rgb / (color.rgb + vec3(1.0));
    fragColor = vec4(mapped, color.a);
}
)";

static const char* s_toneMapExposureFS = R"(#version 300 es
precision highp float;
in vec2 v_uv;
uniform sampler2D u_texture;
uniform float u_exposure;
out vec4 fragColor;
void main() {
    vec4 color = texture(u_texture, v_uv);
    vec3 mapped = vec3(1.0) - exp(-color.rgb * u_exposure);
    fragColor = vec4(mapped, color.a);
}
)";

static const char* s_fxaaFS = R"(#version 300 es
precision highp float;
in vec2 v_uv;
uniform sampler2D u_texture;
uniform vec2 u_resolution;
out vec4 fragColor;

// Simplified FXAA based on Timothy Lottes' FXAA 3.11
// Tuning constants
const float FXAA_REDUCE_MIN = 1.0 / 128.0;
const float FXAA_REDUCE_MUL = 1.0 / 8.0;
const float FXAA_SPAN_MAX   = 8.0;

void main() {
    vec2 texel = 1.0 / u_resolution;

    // Sample centre and 4 diagonal neighbours
    vec3 rgbNW = texture(u_texture, v_uv + vec2(-1.0, -1.0) * texel).rgb;
    vec3 rgbNE = texture(u_texture, v_uv + vec2( 1.0, -1.0) * texel).rgb;
    vec3 rgbSW = texture(u_texture, v_uv + vec2(-1.0,  1.0) * texel).rgb;
    vec3 rgbSE = texture(u_texture, v_uv + vec2( 1.0,  1.0) * texel).rgb;
    vec3 rgbM  = texture(u_texture, v_uv).rgb;

    // Luma (perceptual)
    vec3 luma = vec3(0.299, 0.587, 0.114);
    float lumaNW = dot(rgbNW, luma);
    float lumaNE = dot(rgbNE, luma);
    float lumaSW = dot(rgbSW, luma);
    float lumaSE = dot(rgbSE, luma);
    float lumaM  = dot(rgbM,  luma);

    float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));

    // Edge detection direction
    vec2 dir;
    dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
    dir.y =  ((lumaNW + lumaSW) - (lumaNE + lumaSE));

    float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * 0.25 * FXAA_REDUCE_MUL, FXAA_REDUCE_MIN);
    float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);

    dir = min(vec2(FXAA_SPAN_MAX), max(vec2(-FXAA_SPAN_MAX), dir * rcpDirMin)) * texel;

    // Two-tap filter along edge direction
    vec3 rgbA = 0.5 * (
        texture(u_texture, v_uv + dir * (1.0 / 3.0 - 0.5)).rgb +
        texture(u_texture, v_uv + dir * (2.0 / 3.0 - 0.5)).rgb
    );

    // Four-tap filter for wider sampling
    vec3 rgbB = rgbA * 0.5 + 0.25 * (
        texture(u_texture, v_uv + dir * -0.5).rgb +
        texture(u_texture, v_uv + dir *  0.5).rgb
    );

    float lumaB = dot(rgbB, luma);

    // If the wider sample landed outside the local luma range, use the narrower one
    if (lumaB < lumaMin || lumaB > lumaMax) {
        fragColor = vec4(rgbA, 1.0);
    } else {
        fragColor = vec4(rgbB, 1.0);
    }
}
)";

static const char* s_vignetteFS = R"(#version 300 es
precision highp float;
in vec2 v_uv;
uniform sampler2D u_texture;
uniform float u_intensity;
out vec4 fragColor;
void main() {
    vec4 color = texture(u_texture, v_uv);
    vec2 center = v_uv - 0.5;
    float dist = length(center);
    float vignette = 1.0 - smoothstep(0.5 - u_intensity, 0.5 + u_intensity, dist);
    fragColor = vec4(color.rgb * vignette, color.a);
}
)";

static const char* s_chromaticFS = R"(#version 300 es
precision highp float;
in vec2 v_uv;
uniform sampler2D u_texture;
uniform float u_offset;
out vec4 fragColor;
void main() {
    vec2 dir = v_uv - 0.5;
    float r = texture(u_texture, v_uv + dir * u_offset).r;
    float g = texture(u_texture, v_uv).g;
    float b = texture(u_texture, v_uv - dir * u_offset).b;
    fragColor = vec4(r, g, b, 1.0);
}
)";

static const char* s_grainFS = R"(#version 300 es
precision highp float;
in vec2 v_uv;
uniform sampler2D u_texture;
uniform float u_amount;
uniform float u_time;
out vec4 fragColor;
float random(vec2 st) {
    return fract(sin(dot(st.xy, vec2(12.9898, 78.233))) * 43758.5453123);
}
void main() {
    vec4 color = texture(u_texture, v_uv);
    float grain = random(v_uv + u_time) * u_amount;
    fragColor = vec4(color.rgb + grain - u_amount * 0.5, color.a);
}
)";

static const char* s_gaussBlurFS = R"(#version 300 es
precision highp float;
in vec2 v_uv;
uniform sampler2D u_texture;
uniform float u_radius;
uniform vec2 u_direction;
out vec4 fragColor;
void main() {
    vec2 texelSize = 1.0 / vec2(textureSize(u_texture, 0));
    vec4 color = vec4(0.0);
    float weights[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);
    for (int i = -4; i <= 4; i++) {
        color += texture(u_texture, v_uv + float(i) * u_radius * u_direction * texelSize) * weights[abs(i)];
    }
    fragColor = color;
}
)";

static const char* s_outlineFS = R"(#version 300 es
precision highp float;
in vec2 v_uv;
uniform sampler2D u_texture;
uniform vec4 u_color;
uniform float u_thickness;
uniform vec2 u_resolution;
out vec4 fragColor;
void main() {
    vec2 texel = 1.0 / u_resolution;
    float depth = texture(u_texture, v_uv).r;
    float depthL = texture(u_texture, v_uv - vec2(texel.x * u_thickness, 0.0)).r;
    float depthR = texture(u_texture, v_uv + vec2(texel.x * u_thickness, 0.0)).r;
    float depthT = texture(u_texture, v_uv + vec2(0.0, texel.y * u_thickness)).r;
    float depthB = texture(u_texture, v_uv - vec2(0.0, texel.y * u_thickness)).r;
    float diff = abs(depthL - depth) + abs(depthR - depth) + abs(depthT - depth) + abs(depthB - depth);
    if (diff > 0.01) {
        fragColor = u_color;
    } else {
        fragColor = vec4(0.0);
    }
}
)";

static const char* s_pixelateFS = R"(#version 300 es
precision highp float;
in vec2 v_uv;
uniform sampler2D u_texture;
uniform int u_block;
uniform vec2 u_resolution;
out vec4 fragColor;
void main() {
    vec2 blockUV = floor(v_uv * u_resolution / float(u_block)) * float(u_block) / u_resolution;
    fragColor = texture(u_texture, blockUV);
}
)";

static const char* s_colorGradeFS = R"(#version 300 es
precision highp float;
in vec2 v_uv;
uniform sampler2D u_texture;
uniform sampler2D u_lut;
out vec4 fragColor;
void main() {
    vec4 color = texture(u_texture, v_uv);
    vec3 lutCoord = color.rgb;
    fragColor = vec4(texture(u_lut, lutCoord.xy + lutCoord.z * 0.5).rgb, color.a);
}
)";

static Shader createBlitShader(const char* vs, const char* fs) {
    return CreateShader({ ShaderPass{"DEFAULT", vs, fs} });
}

// Cached fullscreen triangle — created once, reused every frame.
// Uses shared_ptr internally (Mesh is value-semantic), so the static
// keeps the GL resources alive for the lifetime of the process.
static Mesh& cachedFullscreenTri() {
    static Mesh s_tri = GenFullscreenTri();
    return s_tri;
}

static void blitToTarget(Texture src, RenderTarget dst, Shader& shader) {
    if (dst.Valid()) {
        glBindFramebuffer(GL_FRAMEBUFFER, dst.GL());
        glViewport(0, 0, dst.W(), dst.H());
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, ScreenW(), ScreenH());
    }
    
    shader.use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, src.GL());
    shader.setInt("u_texture", 0);
    
    const Mesh& tri = cachedFullscreenTri();
    glBindVertexArray(tri.GL());
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}

void Blit(Texture src, RenderTarget dst, Material mat) {
    if (!src.Valid()) return;
    
    Shader* shaderPtr = mat.GetShader();
    if (!shaderPtr || !shaderPtr->Ready()) return;
    
    if (dst.Valid()) {
        glBindFramebuffer(GL_FRAMEBUFFER, dst.GL());
        glViewport(0, 0, dst.W(), dst.H());
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, ScreenW(), ScreenH());
    }
    
    shaderPtr->use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, src.GL());
    shaderPtr->setInt("u_texture", 0);
    
    const Mesh& tri = cachedFullscreenTri();
    glBindVertexArray(tri.GL());
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}

// Lazily-created blit shader. Uses shared_ptr so that cleanup is controlled
// and doesn't depend on static destruction order (GL context may already be
// destroyed at that point).  The shader is created on first use and recreated
// if it somehow becomes invalid.
static Shader& getBlitShader() {
    static Shader s_shader;
    if (!s_shader.Valid()) {
        s_shader = createBlitShader(s_blitVS, s_blitFS);
    }
    return s_shader;
}

void Blit(Texture src, RenderTarget dst) {
    if (!src.Valid()) return;
    
    Shader& shader = getBlitShader();
    blitToTarget(src, dst, shader);
}

void BlitToScreen(Texture src) {
    Blit(src, ScreenTarget());
}

void BlitToScreen(Texture src, Material mat) {
    Blit(src, ScreenTarget(), mat);
}

void BlitToScreen(Texture src, Rect dst_rect) {
    if (!src.Valid()) return;
    
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(static_cast<GLint>(dst_rect.x), static_cast<GLint>(dst_rect.y),
               static_cast<GLsizei>(dst_rect.w), static_cast<GLsizei>(dst_rect.h));
    
    static Shader& shader = getBlitShader();
    shader.use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, src.GL());
    shader.setInt("u_texture", 0);
    
    const Mesh& tri = cachedFullscreenTri();
    glBindVertexArray(tri.GL());
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}

void BlitMulti(std::initializer_list<Texture> inputs, RenderTarget dst, Material mat) {
    if (!mat.Valid()) return;
    
    if (dst.Valid()) {
        glBindFramebuffer(GL_FRAMEBUFFER, dst.GL());
        glViewport(0, 0, dst.W(), dst.H());
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, ScreenW(), ScreenH());
    }
    
    Shader* shaderPtr = mat.GetShader();
    if (!shaderPtr || !shaderPtr->Ready()) return;
    
    shaderPtr->use();
    
    int slot = 0;
    for (auto& tex : inputs) {
        if (tex.Valid()) {
            glActiveTexture(GL_TEXTURE0 + slot);
            glBindTexture(GL_TEXTURE_2D, tex.GL());
            std::string uniformName = slot == 0 ? "u_texture" : "u_texture" + std::to_string(slot);
            shaderPtr->setInt(uniformName, slot);
            slot++;
        }
    }
    
    const Mesh& tri = cachedFullscreenTri();
    glBindVertexArray(tri.GL());
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}

StageDesc FxBloom(Texture src, RenderTarget dst, float threshold, float intensity, int iterations) {
    StageDesc stage;
    stage.name = "Bloom";
    stage.type = StageType::Blit;
    stage.target = dst;
    stage.blit_material = CreateMat(createBlitShader(s_blitVS, s_bloomCombineFS));
    stage.AddBlitInput(src);
    return stage;
}

StageDesc FxToneMapACES(Texture src, RenderTarget dst) {
    StageDesc stage;
    stage.name = "ToneMapACES";
    stage.type = StageType::Blit;
    stage.target = dst;
    stage.blit_material = CreateMat(createBlitShader(s_blitVS, s_toneMapACESFS));
    stage.AddBlitInput(src);
    return stage;
}

StageDesc FxToneMapReinhard(Texture src, RenderTarget dst) {
    StageDesc stage;
    stage.name = "ToneMapReinhard";
    stage.type = StageType::Blit;
    stage.target = dst;
    stage.blit_material = CreateMat(createBlitShader(s_blitVS, s_toneMapReinhardFS));
    stage.AddBlitInput(src);
    return stage;
}

StageDesc FxToneMapExposure(Texture src, RenderTarget dst, float exposure) {
    StageDesc stage;
    stage.name = "ToneMapExposure";
    stage.type = StageType::Blit;
    stage.target = dst;
    stage.blit_material = CreateMat(createBlitShader(s_blitVS, s_toneMapExposureFS));
    stage.AddBlitInput(src);
    stage.blit_material.Set("u_exposure", exposure);
    return stage;
}

StageDesc FxFXAA(Texture src, RenderTarget dst) {
    StageDesc stage;
    stage.name = "FXAA";
    stage.type = StageType::Blit;
    stage.target = dst;
    stage.blit_material = CreateMat(createBlitShader(s_blitVS, s_fxaaFS));
    stage.AddBlitInput(src);
    stage.blit_material.Set("u_resolution", Vec2(static_cast<float>(ScreenW()), static_cast<float>(ScreenH())));
    return stage;
}

StageDesc FxSSAO(Texture depth_tex, Texture normal_tex, RenderTarget dst, int samples, float radius) {
    StageDesc stage;
    stage.name = "SSAO";
    stage.type = StageType::Blit;
    stage.target = dst;
    stage.blit_material = CreateMat(createBlitShader(s_blitVS, s_blitFS));
    stage.AddBlitInput(depth_tex);
    stage.AddBlitInput(normal_tex);
    return stage;
}

StageDesc FxVignette(Texture src, RenderTarget dst, float intensity) {
    StageDesc stage;
    stage.name = "Vignette";
    stage.type = StageType::Blit;
    stage.target = dst;
    stage.blit_material = CreateMat(createBlitShader(s_blitVS, s_vignetteFS));
    stage.AddBlitInput(src);
    stage.blit_material.Set("u_intensity", intensity);
    return stage;
}

StageDesc FxChromatic(Texture src, RenderTarget dst, float offset) {
    StageDesc stage;
    stage.name = "Chromatic";
    stage.type = StageType::Blit;
    stage.target = dst;
    stage.blit_material = CreateMat(createBlitShader(s_blitVS, s_chromaticFS));
    stage.AddBlitInput(src);
    stage.blit_material.Set("u_offset", offset);
    return stage;
}

StageDesc FxGrain(Texture src, RenderTarget dst, float amount) {
    StageDesc stage;
    stage.name = "Grain";
    stage.type = StageType::Blit;
    stage.target = dst;
    stage.blit_material = CreateMat(createBlitShader(s_blitVS, s_grainFS));
    stage.AddBlitInput(src);
    stage.blit_material.Set("u_amount", amount);
    stage.blit_material.Set("u_time", static_cast<float>(Time()));
    return stage;
}

StageDesc FxGaussBlur(Texture src, RenderTarget dst, float radius) {
    StageDesc stage;
    stage.name = "GaussBlur";
    stage.type = StageType::Blit;
    stage.target = dst;
    stage.blit_material = CreateMat(createBlitShader(s_blitVS, s_gaussBlurFS));
    stage.AddBlitInput(src);
    stage.blit_material.Set("u_radius", radius);
    stage.blit_material.Set("u_direction", Vec2(1.0f, 0.0f));
    return stage;
}

StageDesc FxKawaseBlur(Texture src, RenderTarget dst, int iterations) {
    StageDesc stage;
    stage.name = "KawaseBlur";
    stage.type = StageType::Blit;
    stage.target = dst;
    stage.blit_material = CreateMat(createBlitShader(s_blitVS, s_gaussBlurFS));
    stage.AddBlitInput(src);
    return stage;
}

StageDesc FxPixelate(Texture src, RenderTarget dst, int block) {
    StageDesc stage;
    stage.name = "Pixelate";
    stage.type = StageType::Blit;
    stage.target = dst;
    stage.blit_material = CreateMat(createBlitShader(s_blitVS, s_pixelateFS));
    stage.AddBlitInput(src);
    stage.blit_material.Set("u_block", block);
    stage.blit_material.Set("u_resolution", Vec2(static_cast<float>(ScreenW()), static_cast<float>(ScreenH())));
    return stage;
}

StageDesc FxOutline(Texture depth_tex, RenderTarget dst, Color color, float thickness) {
    StageDesc stage;
    stage.name = "Outline";
    stage.type = StageType::Blit;
    stage.target = dst;
    stage.blit_material = CreateMat(createBlitShader(s_blitVS, s_outlineFS));
    stage.AddBlitInput(depth_tex);
    stage.blit_material.Set("u_color", color);
    stage.blit_material.Set("u_thickness", thickness);
    stage.blit_material.Set("u_resolution", Vec2(static_cast<float>(ScreenW()), static_cast<float>(ScreenH())));
    return stage;
}

StageDesc FxColorGrade(Texture src, RenderTarget dst, Texture lut) {
    StageDesc stage;
    stage.name = "ColorGrade";
    stage.type = StageType::Blit;
    stage.target = dst;
    stage.blit_material = CreateMat(createBlitShader(s_blitVS, s_colorGradeFS));
    stage.AddBlitInput(src);
    if (lut.Valid()) {
        stage.blit_material.SetTex("u_lut", lut, 1);
    }
    return stage;
}

StageDesc FxCustom(Texture src, RenderTarget dst, Material mat) {
    StageDesc stage;
    stage.name = "Custom";
    stage.type = StageType::Blit;
    stage.target = dst;
    stage.blit_material = mat;
    stage.AddBlitInput(src);
    return stage;
}

StageDesc FxCustomMulti(std::initializer_list<Texture> inputs, RenderTarget dst, Material mat) {
    StageDesc stage;
    stage.name = "CustomMulti";
    stage.type = StageType::Blit;
    stage.target = dst;
    stage.blit_material = mat;
    for (auto& tex : inputs) {
        stage.AddBlitInput(tex);
    }
    return stage;
}

Material BloomMat(float threshold, float intensity) {
    Material mat = CreateMat(createBlitShader(s_blitVS, s_bloomCombineFS));
    mat.Set("u_threshold", threshold);
    mat.Set("u_intensity", intensity);
    return mat;
}

Material ToneMapACESmat() {
    return CreateMat(createBlitShader(s_blitVS, s_toneMapACESFS));
}

Material ToneMapReinhardMat() {
    return CreateMat(createBlitShader(s_blitVS, s_toneMapReinhardFS));
}

Material FXAAMat() {
    Material mat = CreateMat(createBlitShader(s_blitVS, s_fxaaFS));
    mat.Set("u_resolution", Vec2(static_cast<float>(ScreenW()), static_cast<float>(ScreenH())));
    return mat;
}

Material SSAOMat(int samples, float radius) {
    Material mat = CreateMat(createBlitShader(s_blitVS, s_blitFS));
    return mat;
}

Material OutlineMat(Color color, float thickness) {
    Material mat = CreateMat(createBlitShader(s_blitVS, s_outlineFS));
    mat.Set("u_color", color);
    mat.Set("u_thickness", thickness);
    mat.Set("u_resolution", Vec2(static_cast<float>(ScreenW()), static_cast<float>(ScreenH())));
    return mat;
}

Material GaussBlurMat(float radius) {
    Material mat = CreateMat(createBlitShader(s_blitVS, s_gaussBlurFS));
    mat.Set("u_radius", radius);
    return mat;
}

}
