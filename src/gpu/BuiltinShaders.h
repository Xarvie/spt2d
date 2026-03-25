#pragma once

#include "../Types.h"

namespace spt3d {
namespace shaders {

// =========================================================================
//  Unlit Shader - Simple color/texture rendering
//  GLSL ES 3.0 compatible
// =========================================================================

constexpr const char* kUnlitVS = R"(#version 300 es
precision highp float;

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv0;
layout(location = 3) in vec4 a_color;
layout(location = 4) in vec4 a_tangent;

uniform mat4 u_model;
uniform mat4 u_view;
uniform mat4 u_proj;

out vec3 v_position;
out vec2 v_uv0;
out vec4 v_color;

void main() {
    v_position = (u_model * vec4(a_position, 1.0)).xyz;
    v_uv0 = a_uv0;
    v_color = a_color;
    gl_Position = u_proj * u_view * vec4(v_position, 1.0);
}
)";

constexpr const char* kUnlitFS = R"(#version 300 es
precision highp float;

in vec3 v_position;
in vec2 v_uv0;
in vec4 v_color;

uniform vec4 u_color;
uniform sampler2D u_texture;

out vec4 fragColor;

void main() {
    vec4 texColor = texture(u_texture, v_uv0);
    fragColor = u_color * v_color * texColor;
}
)";

// =========================================================================
//  Phong Shader - Basic lighting
//  GLSL ES 3.0 compatible
// =========================================================================

constexpr const char* kPhongVS = R"(#version 300 es
precision highp float;

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv0;
layout(location = 3) in vec4 a_color;
layout(location = 4) in vec4 a_tangent;

uniform mat4 u_model;
uniform mat4 u_view;
uniform mat4 u_proj;
uniform mat3 u_normalMatrix;

out vec3 v_worldPos;
out vec3 v_normal;
out vec2 v_uv0;
out vec4 v_color;

void main() {
    vec4 worldPos = u_model * vec4(a_position, 1.0);
    v_worldPos = worldPos.xyz;
    v_normal = normalize(u_normalMatrix * a_normal);
    v_uv0 = a_uv0;
    v_color = a_color;
    gl_Position = u_proj * u_view * worldPos;
}
)";

constexpr const char* kPhongFS = R"(#version 300 es
precision highp float;

in vec3 v_worldPos;
in vec3 v_normal;
in vec2 v_uv0;
in vec4 v_color;

uniform vec3 u_cameraPos;
uniform vec3 u_lightDir;
uniform vec3 u_lightColor;
uniform vec3 u_ambientColor;
uniform vec4 u_albedoColor;
uniform float u_shininess;
uniform sampler2D u_albedoMap;
uniform sampler2D u_normalMap;

out vec4 fragColor;

void main() {
    vec3 N = normalize(v_normal);
    vec3 L = normalize(-u_lightDir);
    vec3 V = normalize(u_cameraPos - v_worldPos);
    vec3 H = normalize(L + V);
    
    vec4 albedo = texture(u_albedoMap, v_uv0) * u_albedoColor * v_color;
    
    float NdotL = max(dot(N, L), 0.0);
    float NdotH = max(dot(N, H), 0.0);
    
    vec3 ambient = u_ambientColor * albedo.rgb;
    vec3 diffuse = u_lightColor * albedo.rgb * NdotL;
    vec3 specular = u_lightColor * pow(NdotH, u_shininess) * 0.5;
    
    fragColor = vec4(ambient + diffuse + specular, albedo.a);
}
)";

// =========================================================================
//  Blit Shader - Fullscreen copy/postprocess
//  GLSL ES 3.0 compatible
// =========================================================================

constexpr const char* kBlitVS = R"(#version 300 es
precision highp float;

layout(location = 0) in vec3 a_position;
layout(location = 2) in vec2 a_uv0;

out vec2 v_uv;

void main() {
    v_uv = a_uv0;
    gl_Position = vec4(a_position.xy, 0.0, 1.0);
}
)";

constexpr const char* kBlitFS = R"(#version 300 es
precision highp float;

in vec2 v_uv;

uniform sampler2D u_texture;

out vec4 fragColor;

void main() {
    fragColor = texture(u_texture, v_uv);
}
)";

// =========================================================================
//  Depth Shader - Depth pre-pass
//  GLSL ES 3.0 compatible
// =========================================================================

constexpr const char* kDepthVS = R"(#version 300 es
precision highp float;

layout(location = 0) in vec3 a_position;

uniform mat4 u_model;
uniform mat4 u_viewProj;

void main() {
    gl_Position = u_viewProj * u_model * vec4(a_position, 1.0);
}
)";

constexpr const char* kDepthFS = R"(#version 300 es
precision highp float;

out vec4 fragColor;

void main() {
    fragColor = vec4(1.0);
}
)";

// =========================================================================
//  Skybox Shader - Cubemap rendering
//  GLSL ES 3.0 compatible
// =========================================================================

constexpr const char* kSkyboxVS = R"(#version 300 es
precision highp float;

layout(location = 0) in vec3 a_position;

uniform mat4 u_view;
uniform mat4 u_proj;

out vec3 v_texCoord;

void main() {
    v_texCoord = a_position;
    mat4 viewNoTranslate = mat4(mat3(u_view));
    vec4 pos = u_proj * viewNoTranslate * vec4(a_position, 1.0);
    gl_Position = pos.xyww;
}
)";

constexpr const char* kSkyboxFS = R"(#version 300 es
precision highp float;

in vec3 v_texCoord;

uniform samplerCube u_envMap;

out vec4 fragColor;

void main() {
    fragColor = texture(u_envMap, v_texCoord);
}
)";

} // namespace shaders
} // namespace spt3d
