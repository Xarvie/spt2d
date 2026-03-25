#pragma once

namespace spt3d {
namespace gfx {

#if defined(__APPLE__)
#define SPT_GFX_GLSL_VERSION "#version 410 core\n"
#else
#define SPT_GFX_GLSL_VERSION "#version 300 es\nprecision mediump float;\n"
#endif

inline const char* const SHADER_VS_BASIC = SPT_GFX_GLSL_VERSION R"(
    in vec2 a_position;
    in vec4 a_color;

    uniform mat4 u_mvp;

    out vec4 v_color;

    void main() {
        gl_Position = u_mvp * vec4(a_position, 0.0, 1.0);
        v_color = a_color;
    }
)";

inline const char* const SHADER_FS_BASIC = SPT_GFX_GLSL_VERSION R"(
    in vec4 v_color;
    out vec4 fragColor;

    void main() {
        fragColor = v_color;
    }
)";

inline const char* const SHADER_VS_TEXTURE = SPT_GFX_GLSL_VERSION R"(
    in vec2 a_position;
    in vec2 a_texcoord;
    in vec4 a_color;

    uniform mat4 u_mvp;

    out vec2 v_texcoord;
    out vec4 v_color;

    void main() {
        gl_Position = u_mvp * vec4(a_position, 0.0, 1.0);
        v_texcoord = a_texcoord;
        v_color = a_color;
    }
)";

inline const char* const SHADER_FS_TEXTURE = SPT_GFX_GLSL_VERSION R"(
    uniform sampler2D u_texture;

    in vec2 v_texcoord;
    in vec4 v_color;
    out vec4 fragColor;

    void main() {
        fragColor = texture(u_texture, v_texcoord) * v_color;
    }
)";

} // namespace gfx
} // namespace spt3d
