#include "../Spt3D.h"
#include "../graphics/Shader.h"
#include "../vfs/VirtualFileSystem.h"

#include <unordered_map>

namespace spt3d {

    struct Shader::Impl {
        struct PassData {
            gfx::GLShader program;
            std::string name;
            std::optional<RenderState> state;
        };
        std::vector<PassData> passes;
        std::unordered_map<std::string, int> passIndex;
        bool valid = false;

        int findPass(std::string_view name) const {
            auto it = passIndex.find(std::string(name));
            return it != passIndex.end() ? it->second : -1;
        }
    };

    bool Shader::Valid() const { return p && p->valid; }
    bool Shader::Ready() const { return Valid(); }
    ResState Shader::State() const { return Valid() ? ResState::Ready : ResState::Failed; }
    bool Shader::HasPass(std::string_view name) const { return p && p->findPass(name) >= 0; }
    int Shader::PassCount() const { return p ? static_cast<int>(p->passes.size()) : 0; }
    Shader::operator bool() const { return Valid(); }

    int Shader::GetLoc(std::string_view pass, std::string_view uniform) const {
        if (!p) return -1;
        int idx = p->findPass(pass);
        if (idx < 0) return -1;
        return p->passes[idx].program.uniformLocation(uniform);
    }

    Shader CreateShader(std::initializer_list<ShaderPass> passes) {
        Shader shader;
        shader.p = std::make_shared<Shader::Impl>();

        int idx = 0;
        for (const auto& pass : passes) {
            Shader::Impl::PassData pd;
            pd.name = pass.name;
            pd.state = pass.state;

            std::string vs(pass.vs);
            std::string fs(pass.fs);

            if (!pd.program.loadFromSource(vs, fs)) {
                return shader;
            }

            shader.p->passes.push_back(std::move(pd));
            shader.p->passIndex[pass.name] = idx++;
        }

        shader.p->valid = true;
        return shader;
    }

    Shader CreateSimpleShader(std::string_view vs, std::string_view fs) {
        return CreateShader({ ShaderPass{"DEFAULT", vs, fs} });
    }

    Shader LoadShader(std::string_view url, Callback cb) {
        Shader shader;
        shader.p = std::make_shared<Shader::Impl>();
        auto impl = shader.p;

        spt3d::VirtualFileSystem::Instance().read(std::string(url),
                                                  [impl, cb](const uint8_t* data, size_t size, bool success) {
                                                      if (success && data && impl) {
                                                          std::string content(reinterpret_cast<const char*>(data), size);

                                                          size_t vsStart = content.find("[VERTEX]");
                                                          size_t fsStart = content.find("[FRAGMENT]");

                                                          if (vsStart != std::string::npos && fsStart != std::string::npos) {
                                                              std::string vs = content.substr(vsStart + 8, fsStart - vsStart - 8);
                                                              std::string fs = content.substr(fsStart + 10);

                                                              Shader::Impl::PassData pd;
                                                              pd.name = "DEFAULT";
                                                              if (pd.program.loadFromSource(vs, fs)) {
                                                                  impl->passes.push_back(std::move(pd));
                                                                  impl->passIndex["DEFAULT"] = 0;
                                                                  impl->valid = true;
                                                                  if (cb) cb(true);
                                                                  return;
                                                              }
                                                          }
                                                      }
                                                      if (cb) cb(false);
                                                  });

        return shader;
    }

    static const char* s_defaultVS = R"(#version 300 es
precision highp float;
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec4 a_color;
layout(location = 2) in vec2 a_texcoord;
uniform mat4 u_mvp;
out vec4 v_color;
out vec2 v_uv;
void main() {
    gl_Position = u_mvp * vec4(a_position, 1.0);
    v_color = a_color;
    v_uv = a_texcoord;
}
)";

    static const char* s_defaultFS = R"(#version 300 es
precision highp float;
in vec4 v_color;
in vec2 v_uv;
uniform sampler2D u_tex;
out vec4 fragColor;
void main() {
    fragColor = v_color * texture(u_tex, v_uv);
}
)";

    static const char* s_default3DVS = R"(#version 300 es
precision highp float;
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_texcoord;
uniform mat4 u_model;
uniform mat4 u_view;
uniform mat4 u_proj;
uniform mat3 u_normalMatrix;
out vec3 v_worldPos;
out vec3 v_normal;
out vec2 v_uv;
void main() {
    vec4 worldPos = u_model * vec4(a_position, 1.0);
    v_worldPos = worldPos.xyz;
    v_normal = u_normalMatrix * a_normal;
    v_uv = a_texcoord;
    gl_Position = u_proj * u_view * worldPos;
}
)";

    static const char* s_default3DFS = R"(#version 300 es
precision highp float;
in vec3 v_worldPos;
in vec3 v_normal;
in vec2 v_uv;
uniform vec3 u_lightDir;
uniform vec3 u_lightColor;
uniform vec3 u_ambient;
uniform vec4 u_color;
uniform sampler2D u_tex;
out vec4 fragColor;
void main() {
    vec3 N = normalize(v_normal);
    float diff = max(dot(N, normalize(u_lightDir)), 0.0);
    vec3 color = u_ambient + u_lightColor * diff;
    fragColor = u_color * texture(u_tex, v_uv) * vec4(color, 1.0);
}
)";

    static const char* s_spriteVS = R"(#version 300 es
precision highp float;
layout(location = 0) in vec3 a_position;
layout(location = 2) in vec2 a_texcoord;
uniform mat4 u_mvp;
out vec2 v_uv;
void main() {
    gl_Position = u_mvp * vec4(a_position.xy, 0.0, 1.0);
    v_uv = a_texcoord;
}
)";

    static const char* s_spriteFS = R"(#version 300 es
precision highp float;
in vec2 v_uv;
uniform vec4 u_color;
uniform sampler2D u_tex;
out vec4 fragColor;
void main() {
    fragColor = u_color * texture(u_tex, v_uv);
}
)";

    Shader DefaultShader() {
        return CreateSimpleShader(s_defaultVS, s_defaultFS);
    }

    Shader DefaultShader3D() {
        return CreateSimpleShader(s_default3DVS, s_default3DFS);
    }

    Shader PBRShader() {
        return DefaultShader3D();
    }

    Shader UnlitShader() {
        return CreateShader({ ShaderPass{"DEFAULT", s_defaultVS, s_defaultFS} });
    }

    Shader SpriteShader() {
        return CreateSimpleShader(s_spriteVS, s_spriteFS);
    }

    Shader SkyboxShader() {
        return DefaultShader3D();
    }

    void Shader::setInt(std::string_view name, int value) const {
        if (!p || p->passes.empty()) return;
        p->passes[0].program.setInt(name, value);
    }

    void Shader::setFloat(std::string_view name, float value) const {
        if (!p || p->passes.empty()) return;
        p->passes[0].program.setFloat(name, value);
    }

    void Shader::setVec2(std::string_view name, float x, float y) const {
        if (!p || p->passes.empty()) return;
        p->passes[0].program.setVec2(name, x, y);
    }

    void Shader::setVec3(std::string_view name, float x, float y, float z) const {
        if (!p || p->passes.empty()) return;
        p->passes[0].program.setVec3(name, x, y, z);
    }

    void Shader::setVec4(std::string_view name, float x, float y, float z, float w) const {
        if (!p || p->passes.empty()) return;
        p->passes[0].program.setVec4(name, x, y, z, w);
    }

    void Shader::setMat4(std::string_view name, const float* matrix) const {
        if (!p || p->passes.empty()) return;
        p->passes[0].program.setMat4(name, matrix);
    }

    void Shader::use() const {
        if (!p || p->passes.empty()) return;
        p->passes[0].program.use();
    }

}