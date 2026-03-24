#include "../Spt3D.h"
#include "../graphics/Shader.h"
#include "../glad/glad.h"

#include <unordered_map>
#include <variant>
#include <vector>
#include <cstring>

namespace spt3d {

struct Material::Impl {
    Shader shader;
    RenderState state;
    std::string tag;
    bool valid = false;
    
    struct UniformValue {
        enum Type { Int, Float, Vec2, Vec3, Vec4, Mat3_, Mat4_, Tex, FloatArray };
        Type type;
        union {
            int intVal;
            float floatVal;
            float vec2Val[2];
            float vec3Val[3];
            float vec4Val[4];
            float mat3Val[9];
            float mat4Val[16];
            int texSlot;
        };
        std::shared_ptr<Texture> texPtr;
        std::vector<float> floatArray;
        int count = 1;
        
        UniformValue() : type(Int), intVal(0) {}
        UniformValue(const UniformValue& other) : type(other.type), texPtr(other.texPtr), floatArray(other.floatArray), count(other.count) {
            switch (type) {
                case Int: intVal = other.intVal; break;
                case Float: floatVal = other.floatVal; break;
                case Vec2: std::memcpy(vec2Val, other.vec2Val, sizeof(vec2Val)); break;
                case Vec3: std::memcpy(vec3Val, other.vec3Val, sizeof(vec3Val)); break;
                case Vec4: std::memcpy(vec4Val, other.vec4Val, sizeof(vec4Val)); break;
                case Mat3_: std::memcpy(mat3Val, other.mat3Val, sizeof(mat3Val)); break;
                case Mat4_: std::memcpy(mat4Val, other.mat4Val, sizeof(mat4Val)); break;
                case Tex: texSlot = other.texSlot; break;
                case FloatArray: break;
            }
        }
        UniformValue& operator=(const UniformValue& other) {
            if (this != &other) {
                type = other.type;
                texPtr = other.texPtr;
                floatArray = other.floatArray;
                count = other.count;
                switch (type) {
                    case Int: intVal = other.intVal; break;
                    case Float: floatVal = other.floatVal; break;
                    case Vec2: std::memcpy(vec2Val, other.vec2Val, sizeof(vec2Val)); break;
                    case Vec3: std::memcpy(vec3Val, other.vec3Val, sizeof(vec3Val)); break;
                    case Vec4: std::memcpy(vec4Val, other.vec4Val, sizeof(vec4Val)); break;
                    case Mat3_: std::memcpy(mat3Val, other.mat3Val, sizeof(mat3Val)); break;
                    case Mat4_: std::memcpy(mat4Val, other.mat4Val, sizeof(mat4Val)); break;
                    case Tex: texSlot = other.texSlot; break;
                    case FloatArray: break;
                }
            }
            return *this;
        }
    };
    
    std::unordered_map<std::string, UniformValue> uniforms;
    
    void applyToPass(const spt3d::Shader& program) const {
        for (const auto& [name, val] : uniforms) {
            GLint loc = program.uniformLocation(name);
            if (loc == -1) continue;
            
            switch (val.type) {
                case UniformValue::Int:
                    glUniform1i(loc, val.intVal);
                    break;
                case UniformValue::Float:
                    glUniform1f(loc, val.floatVal);
                    break;
                case UniformValue::Vec2:
                    glUniform2fv(loc, 1, val.vec2Val);
                    break;
                case UniformValue::Vec3:
                    glUniform3fv(loc, 1, val.vec3Val);
                    break;
                case UniformValue::Vec4:
                    glUniform4fv(loc, 1, val.vec4Val);
                    break;
                case UniformValue::Mat3_:
                    glUniformMatrix3fv(loc, 1, GL_FALSE, val.mat3Val);
                    break;
                case UniformValue::Mat4_:
                    glUniformMatrix4fv(loc, 1, GL_FALSE, val.mat4Val);
                    break;
                case UniformValue::Tex:
                    glActiveTexture(GL_TEXTURE0 + val.texSlot);
                    if (val.texPtr && val.texPtr->Valid()) {
                        glBindTexture(GL_TEXTURE_2D, val.texPtr->GL());
                    }
                    glUniform1i(loc, val.texSlot);
                    break;
                case UniformValue::FloatArray:
                    glUniform1fv(loc, val.count, val.floatArray.data());
                    break;
            }
        }
    }
    
    void applyRenderState() const {
        if (state.blend != BlendMode::Disabled) {
            glEnable(GL_BLEND);
            GLenum srcFactor = GL_SRC_ALPHA;
            GLenum dstFactor = GL_ONE_MINUS_SRC_ALPHA;
            switch (state.blend) {
                case BlendMode::Alpha: break;
                case BlendMode::Additive: srcFactor = GL_ONE; dstFactor = GL_ONE; break;
                case BlendMode::Multiply: srcFactor = GL_DST_COLOR; dstFactor = GL_ZERO; break;
                case BlendMode::PremulAlpha: srcFactor = GL_ONE; dstFactor = GL_ONE_MINUS_SRC_ALPHA; break;
                case BlendMode::Screen: srcFactor = GL_ONE; dstFactor = GL_ONE_MINUS_SRC_COLOR; break;
                default: break;
            }
            glBlendFunc(srcFactor, dstFactor);
            glBlendEquation(GL_FUNC_ADD);
        } else {
            glDisable(GL_BLEND);
        }
        
        if (state.cull != CullFace::None) {
            glEnable(GL_CULL_FACE);
            glCullFace(state.cull == CullFace::Back ? GL_BACK : GL_FRONT);
        } else {
            glDisable(GL_CULL_FACE);
        }
        
        if (state.depth_test) {
            glEnable(GL_DEPTH_TEST);
            GLenum depthFunc = GL_LESS;
            switch (state.depth_func) {
                case DepthFunc::Less: depthFunc = GL_LESS; break;
                case DepthFunc::LEqual: depthFunc = GL_LEQUAL; break;
                case DepthFunc::Greater: depthFunc = GL_GREATER; break;
                case DepthFunc::GEqual: depthFunc = GL_GEQUAL; break;
                case DepthFunc::Equal: depthFunc = GL_EQUAL; break;
                case DepthFunc::Always: depthFunc = GL_ALWAYS; break;
                case DepthFunc::Never: depthFunc = GL_NEVER; break;
            }
            glDepthFunc(depthFunc);
        } else {
            glDisable(GL_DEPTH_TEST);
        }
        
        glDepthMask(state.depth_write ? GL_TRUE : GL_FALSE);
        glColorMask(state.color_write, state.color_write, state.color_write, state.color_write);
        
        if (state.line_width != 1.0f) {
            glLineWidth(state.line_width);
        }
    }
};

bool Material::Valid() const { return p && p->valid; }
Material::operator bool() const { return Valid(); }

Shader* Material::GetShader() const { 
    if (!p || !p->shader.Valid()) return nullptr;
    return &p->shader;
}

void Material::Set(std::string_view name, int v) {
    if (!p) return;
    auto& val = p->uniforms[std::string(name)];
    val.type = Material::Impl::UniformValue::Int;
    val.intVal = v;
}

void Material::Set(std::string_view name, float v) {
    if (!p) return;
    auto& val = p->uniforms[std::string(name)];
    val.type = Material::Impl::UniformValue::Float;
    val.floatVal = v;
}

void Material::Set(std::string_view name, Vec2 v) {
    if (!p) return;
    auto& val = p->uniforms[std::string(name)];
    val.type = Material::Impl::UniformValue::Vec2;
    val.vec2Val[0] = v.x;
    val.vec2Val[1] = v.y;
}

void Material::Set(std::string_view name, Vec3 v) {
    if (!p) return;
    auto& val = p->uniforms[std::string(name)];
    val.type = Material::Impl::UniformValue::Vec3;
    val.vec3Val[0] = v.x;
    val.vec3Val[1] = v.y;
    val.vec3Val[2] = v.z;
}

void Material::Set(std::string_view name, Vec4 v) {
    if (!p) return;
    auto& val = p->uniforms[std::string(name)];
    val.type = Material::Impl::UniformValue::Vec4;
    val.vec4Val[0] = v.x;
    val.vec4Val[1] = v.y;
    val.vec4Val[2] = v.z;
    val.vec4Val[3] = v.w;
}

void Material::Set(std::string_view name, Color v) {
    Set(name, ToVec4(v));
}

void Material::Set(std::string_view name, Mat3 v) {
    if (!p) return;
    auto& val = p->uniforms[std::string(name)];
    val.type = Material::Impl::UniformValue::Mat3_;
    std::memcpy(val.mat3Val, glm::value_ptr(v), sizeof(float) * 9);
}

void Material::Set(std::string_view name, Mat4 v) {
    if (!p) return;
    auto& val = p->uniforms[std::string(name)];
    val.type = Material::Impl::UniformValue::Mat4_;
    std::memcpy(val.mat4Val, glm::value_ptr(v), sizeof(float) * 16);
}

void Material::Set(std::string_view name, const float* data, int count) {
    if (!p || !data || count <= 0) return;
    auto& val = p->uniforms[std::string(name)];
    val.type = Material::Impl::UniformValue::FloatArray;
    val.floatArray.assign(data, data + count);
    val.count = count;
}

void Material::SetTex(std::string_view name, Texture tex, int slot) {
    if (!p) return;
    auto& val = p->uniforms[std::string(name)];
    val.type = Material::Impl::UniformValue::Tex;
    val.texPtr = std::make_shared<Texture>(std::move(tex));
    val.texSlot = slot;
}

void Material::SetAlbedo(Texture tex) { SetTex("u_albedo", std::move(tex), 0); }
void Material::SetAlbedoColor(Color c) { Set("u_albedoColor", c); }
void Material::SetNormalMap(Texture tex) { SetTex("u_normalMap", std::move(tex), 1); }
void Material::SetEmission(Texture tex) { SetTex("u_emission", std::move(tex), 2); }
void Material::SetEmissionStrength(float v) { Set("u_emissionStrength", v); }
void Material::SetMetallicRoughness(Texture tex) { SetTex("u_metallicRoughness", std::move(tex), 3); }
void Material::SetMetallic(float v) { Set("u_metallic", v); }
void Material::SetRoughness(float v) { Set("u_roughness", v); }
void Material::SetOcclusion(Texture tex) { SetTex("u_occlusion", std::move(tex), 4); }

void Material::SetState(RenderState s) { if (p) p->state = s; }
void Material::SetBlend(BlendMode mode) { if (p) p->state.blend = mode; }
void Material::SetCull(CullFace face) { if (p) p->state.cull = face; }
void Material::SetDepthWrite(bool w) { if (p) p->state.depth_write = w; }
void Material::SetDepthTest(bool t) { if (p) p->state.depth_test = t; }

void Material::SetTag(std::string_view tag) { if (p) p->tag = tag; }
std::string_view Material::GetTag() const { return p ? p->tag : ""; }

Material CreateMat(Shader shader) {
    Material mat;
    mat.p = std::make_shared<Material::Impl>();
    mat.p->shader = std::move(shader);
    mat.p->valid = true;
    mat.p->state = RenderState{};
    return mat;
}

Material DefaultMat() {
    return CreateMat(DefaultShader());
}

Material UnlitMat() {
    return CreateMat(UnlitShader());
}

Material PBRMat() {
    return CreateMat(PBRShader());
}

Material CloneMat(Material src) {
    Material mat;
    if (!src.Valid()) return mat;
    mat.p = std::make_shared<Material::Impl>();
    mat.p->shader.p = src.p->shader.p;
    mat.p->state = src.p->state;
    mat.p->tag = src.p->tag;
    mat.p->uniforms = src.p->uniforms;
    mat.p->valid = true;
    return mat;
}

}
