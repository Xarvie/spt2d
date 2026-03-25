#include "../Spt3D.h"
#include "../glad/glad.h"

#include <vector>
#include <unordered_map>
#include <algorithm>
#include <string>

namespace spt3d {

struct Pipeline::Impl {
    std::vector<StageDesc> stages;
    std::unordered_map<std::string, int> stageIndex;
    std::unordered_map<std::string, bool> stageEnabled;
    
    std::unordered_map<std::string, Texture> boundTextures;
    std::unordered_map<std::string, float> boundFloats;
    std::unordered_map<std::string, Vec2> boundVec2s;
    std::unordered_map<std::string, Vec3> boundVec3s;
    std::unordered_map<std::string, Vec4> boundVec4s;
    std::unordered_map<std::string, Mat4> boundMat4s;
};

Pipeline CreatePipeline() {
    Pipeline pipe;
    pipe.p = std::make_shared<Pipeline::Impl>();
    return pipe;
}

void PushStage(Pipeline pipe, StageDesc stage) {
    if (!pipe.p) return;
    std::string name = stage.name;
    pipe.p->stageIndex[name] = static_cast<int>(pipe.p->stages.size());
    pipe.p->stageEnabled[name] = true;
    pipe.p->stages.push_back(std::move(stage));
}

void InsertStage(Pipeline pipe, int index, StageDesc stage) {
    if (!pipe.p) return;
    if (index < 0 || index > static_cast<int>(pipe.p->stages.size())) return;
    
    std::string name = stage.name;
    pipe.p->stages.insert(pipe.p->stages.begin() + index, std::move(stage));
    pipe.p->stageEnabled[name] = true;
    
    pipe.p->stageIndex.clear();
    for (size_t i = 0; i < pipe.p->stages.size(); ++i) {
        pipe.p->stageIndex[pipe.p->stages[i].name] = static_cast<int>(i);
    }
}

void RemoveStage(Pipeline pipe, int index) {
    if (!pipe.p) return;
    if (index < 0 || index >= static_cast<int>(pipe.p->stages.size())) return;
    
    std::string name = pipe.p->stages[index].name;
    pipe.p->stages.erase(pipe.p->stages.begin() + index);
    pipe.p->stageIndex.erase(name);
    pipe.p->stageEnabled.erase(name);
    
    pipe.p->stageIndex.clear();
    for (size_t i = 0; i < pipe.p->stages.size(); ++i) {
        pipe.p->stageIndex[pipe.p->stages[i].name] = static_cast<int>(i);
    }
}

void RemoveStage(Pipeline pipe, std::string_view name) {
    if (!pipe.p) return;
    auto it = pipe.p->stageIndex.find(std::string(name));
    if (it != pipe.p->stageIndex.end()) {
        RemoveStage(pipe, it->second);
    }
}

void EnableStage(Pipeline pipe, std::string_view name, bool on) {
    if (!pipe.p) return;
    pipe.p->stageEnabled[std::string(name)] = on;
}

bool StageEnabled(Pipeline pipe, std::string_view name) {
    if (!pipe.p) return false;
    auto it = pipe.p->stageEnabled.find(std::string(name));
    return it != pipe.p->stageEnabled.end() ? it->second : false;
}

int StageCount(Pipeline pipe) {
    return pipe.p ? static_cast<int>(pipe.p->stages.size()) : 0;
}

StageDesc& GetStage(Pipeline pipe, int index) {
    static StageDesc empty;
    if (!pipe.p || index < 0 || index >= static_cast<int>(pipe.p->stages.size())) {
        return empty;
    }
    return pipe.p->stages[index];
}

StageDesc& GetStage(Pipeline pipe, std::string_view name) {
    static StageDesc empty;
    if (!pipe.p) return empty;
    auto it = pipe.p->stageIndex.find(std::string(name));
    if (it != pipe.p->stageIndex.end()) {
        return pipe.p->stages[it->second];
    }
    return empty;
}

void PipelineBindTex(Pipeline pipe, std::string_view sampler, Texture tex, int slot) {
    if (!pipe.p) return;
    pipe.p->boundTextures[std::string(sampler)] = std::move(tex);
}

void PipelineBindFloat(Pipeline pipe, std::string_view name, float value) {
    if (!pipe.p) return;
    pipe.p->boundFloats[std::string(name)] = value;
}

void PipelineBindVec2(Pipeline pipe, std::string_view name, Vec2 value) {
    if (!pipe.p) return;
    pipe.p->boundVec2s[std::string(name)] = value;
}

void PipelineBindVec3(Pipeline pipe, std::string_view name, Vec3 value) {
    if (!pipe.p) return;
    pipe.p->boundVec3s[std::string(name)] = value;
}

void PipelineBindVec4(Pipeline pipe, std::string_view name, Vec4 value) {
    if (!pipe.p) return;
    pipe.p->boundVec4s[std::string(name)] = value;
}

void PipelineBindMat4(Pipeline pipe, std::string_view name, Mat4 value) {
    if (!pipe.p) return;
    pipe.p->boundMat4s[std::string(name)] = value;
}

void PipelineClearBindings(Pipeline pipe) {
    if (!pipe.p) return;
    pipe.p->boundTextures.clear();
    pipe.p->boundFloats.clear();
    pipe.p->boundVec2s.clear();
    pipe.p->boundVec3s.clear();
    pipe.p->boundVec4s.clear();
    pipe.p->boundMat4s.clear();
}

static void bindRenderTarget(const RenderTarget& rt) {
    if (rt.Valid()) {
        glBindFramebuffer(GL_FRAMEBUFFER, rt.GL());
        glViewport(0, 0, rt.W(), rt.H());
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
}

static void clearRenderTarget(const StageDesc& stage) {
    if (stage.clear_color || stage.clear_depth) {
        GLbitfield mask = 0;
        if (stage.clear_color) {
            mask |= GL_COLOR_BUFFER_BIT;
            glClearColor(stage.clear_color_value.r / 255.0f,
                        stage.clear_color_value.g / 255.0f,
                        stage.clear_color_value.b / 255.0f,
                        stage.clear_color_value.a / 255.0f);
        }
        if (stage.clear_depth) {
            mask |= GL_DEPTH_BUFFER_BIT;
            glClearDepthf(stage.clear_depth_value);
        }
        glClear(mask);
    }
}

// Cached fullscreen triangle for blit stages.
static Mesh& pipelineFullscreenTri() {
    static Mesh s_tri = GenFullscreenTri();
    return s_tri;
}

// Helper: execute a Blit stage (fullscreen pass with material + input textures).
static void executeBlit(const StageDesc& stage) {
    Material mat = stage.blit_material;
    if (!mat.Valid()) return;
    
    Shader* shaderPtr = mat.GetShader();
    if (!shaderPtr || !shaderPtr->Ready()) return;
    
    shaderPtr->use();
    
    // Bind blit input textures to sequential slots.
    int slot = 0;
    for (const auto& tex : stage.blit_inputs) {
        if (tex.Valid()) {
            glActiveTexture(GL_TEXTURE0 + slot);
            glBindTexture(GL_TEXTURE_2D, tex.GL());
            if (slot == 0) {
                shaderPtr->setInt("u_texture", 0);
            } else {
                std::string name = "u_texture" + std::to_string(slot);
                shaderPtr->setInt(name, slot);
            }
            ++slot;
        }
    }
    
    // Apply material uniforms (u_threshold, u_intensity, etc.)
    ApplyMat(mat);
    ApplyMatState(mat);
    
    const Mesh& tri = pipelineFullscreenTri();
    glBindVertexArray(tri.GL());
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}

// Helper: execute a Geometry stage with a 3D camera.
static void executeGeometry3D(const StageDesc& stage, DrawList dl, const Camera3D& cam) {
    auto& items = detail::GetDrawItems(dl);
    if (items.empty()) return;
    
    float aspect = 1.0f;
    if (stage.target.Valid() && stage.target.H() > 0) {
        aspect = static_cast<float>(stage.target.W()) / stage.target.H();
    } else if (ScreenH() > 0) {
        aspect = static_cast<float>(ScreenW()) / ScreenH();
    }
    
    Mat4 view = ViewMat(cam);
    Mat4 proj = ProjMat(cam, aspect);
    
    // Sort front-to-back for opaque geometry.
    detail::SortDrawList(dl, SortMode::FrontToBack, cam.position);
    
    for (auto& item : items) {
        if (!item.mesh.Valid()) continue;
        
        // If the stage has a tag filter, skip non-matching items.
        // (pass_name field is the tag to match)
        if (!stage.pass_name.empty() && item.mat.Valid()) {
            if (item.mat.GetTag() != stage.pass_name) continue;
        }
        
        Shader* shader = item.mat.Valid() ? item.mat.GetShader() : nullptr;
        if (!shader || !shader->Ready()) continue;
        
        shader->use();
        
        Mat4 model = item.transform;
        Mat3 normalMat = glm::transpose(glm::inverse(Mat3(model)));
        
        shader->setMat4("u_model", glm::value_ptr(model));
        shader->setMat4("u_view",  glm::value_ptr(view));
        shader->setMat4("u_proj",  glm::value_ptr(proj));
        
        Mat4 mvp = proj * view * model;
        shader->setMat4("u_mvp", glm::value_ptr(mvp));
        shader->setVec3("u_normalMatrix", glm::value_ptr(normalMat)[0],
                        glm::value_ptr(normalMat)[1], glm::value_ptr(normalMat)[2]);
        
        ApplyMat(item.mat);
        ApplyMatState(item.mat);
        
        glBindVertexArray(item.mesh.GL());
        if (item.mesh.Indices() > 0) {
            glDrawElements(GL_TRIANGLES, item.mesh.Indices(), GL_UNSIGNED_SHORT, nullptr);
        } else {
            glDrawArrays(GL_TRIANGLES, 0, item.mesh.Verts());
        }
    }
    glBindVertexArray(0);
}

// Helper: execute a Geometry stage with a 2D camera.
static void executeGeometry2D(const StageDesc& stage, DrawList dl, const Camera2D& cam) {
    auto& items = detail::GetDrawItems(dl);
    if (items.empty()) return;
    
    int w = stage.target.Valid() ? stage.target.W() : ScreenW();
    int h = stage.target.Valid() ? stage.target.H() : ScreenH();
    
    Mat4 camMat = CamMat2D(cam);
    Mat4 ortho = glm::ortho(0.0f, static_cast<float>(w),
                             static_cast<float>(h), 0.0f, -1.0f, 1.0f);
    
    for (auto& item : items) {
        if (!item.mesh.Valid()) continue;
        
        if (!stage.pass_name.empty() && item.mat.Valid()) {
            if (item.mat.GetTag() != stage.pass_name) continue;
        }
        
        Shader* shader = item.mat.Valid() ? item.mat.GetShader() : nullptr;
        if (!shader || !shader->Ready()) continue;
        
        shader->use();
        
        Mat4 mvp = ortho * camMat * item.transform;
        shader->setMat4("u_mvp", glm::value_ptr(mvp));
        
        ApplyMat(item.mat);
        ApplyMatState(item.mat);
        
        glBindVertexArray(item.mesh.GL());
        if (item.mesh.Indices() > 0) {
            glDrawElements(GL_TRIANGLES, item.mesh.Indices(), GL_UNSIGNED_SHORT, nullptr);
        } else {
            glDrawArrays(GL_TRIANGLES, 0, item.mesh.Verts());
        }
    }
    glBindVertexArray(0);
}

void Execute(Pipeline pipe, DrawList dl, const Camera3D& cam) {
    if (!pipe.p) return;
    
    for (const auto& stage : pipe.p->stages) {
        if (!pipe.p->stageEnabled[stage.name]) continue;
        
        bindRenderTarget(stage.target);
        clearRenderTarget(stage);
        
        switch (stage.type) {
            case StageType::Geometry: {
                executeGeometry3D(stage, dl, cam);
                break;
            }
            case StageType::Blit: {
                executeBlit(stage);
                break;
            }
            case StageType::Custom: {
                if (stage.custom_fn) {
                    stage.custom_fn();
                }
                break;
            }
        }
    }
    
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Execute(Pipeline pipe, DrawList dl, const Camera2D& cam) {
    if (!pipe.p) return;
    
    for (const auto& stage : pipe.p->stages) {
        if (!pipe.p->stageEnabled[stage.name]) continue;
        
        bindRenderTarget(stage.target);
        clearRenderTarget(stage);
        
        switch (stage.type) {
            case StageType::Geometry: {
                executeGeometry2D(stage, dl, cam);
                break;
            }
            case StageType::Blit: {
                executeBlit(stage);
                break;
            }
            case StageType::Custom: {
                if (stage.custom_fn) {
                    stage.custom_fn();
                }
                break;
            }
        }
    }
    
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

}
