#include "Pipeline.h"
#include "../core/GameWork.h"
#include "../glad/glad.h"

#include <algorithm>

namespace spt3d {

void Pipeline::pushStage(StageDesc stage) {
    StageEntry entry;
    entry.desc = std::move(stage);
    entry.enabled = true;
    m_stages.push_back(std::move(entry));
}

void Pipeline::insertStage(int index, StageDesc stage) {
    if (index < 0) index = 0;
    if (index > static_cast<int>(m_stages.size())) index = static_cast<int>(m_stages.size());

    StageEntry entry;
    entry.desc = std::move(stage);
    entry.enabled = true;
    m_stages.insert(m_stages.begin() + index, std::move(entry));
}

void Pipeline::removeStage(int index) {
    if (index >= 0 && index < static_cast<int>(m_stages.size())) {
        m_stages.erase(m_stages.begin() + index);
    }
}

void Pipeline::removeStage(std::string_view name) {
    int32_t hash = HashName(name);
    m_stages.erase(
        std::remove_if(m_stages.begin(), m_stages.end(),
            [hash](const StageEntry& e) {
                return HashName(e.desc.name) == hash;
            }),
        m_stages.end());
}

void Pipeline::enableStage(std::string_view name, bool on) {
    int32_t hash = HashName(name);
    for (auto& entry : m_stages) {
        if (HashName(entry.desc.name) == hash) {
            entry.enabled = on;
            break;
        }
    }
}

bool Pipeline::stageEnabled(std::string_view name) const {
    int32_t hash = HashName(name);
    for (const auto& entry : m_stages) {
        if (HashName(entry.desc.name) == hash) {
            return entry.enabled;
        }
    }
    return false;
}

int Pipeline::stageCount() const {
    return static_cast<int>(m_stages.size());
}

StageDesc& Pipeline::stage(int index) {
    return m_stages.at(index).desc;
}

StageDesc& Pipeline::stage(std::string_view name) {
    int32_t hash = HashName(name);
    for (auto& entry : m_stages) {
        if (HashName(entry.desc.name) == hash) {
            return entry.desc;
        }
    }
    static StageDesc dummy;
    return dummy;
}

void Pipeline::emit(GameWork& work, const DrawList& drawList,
                    const Camera3D& cam, float aspect) const {
    work.setCamera(cam, aspect, work.frameData.time, work.frameData.deltaTime);

    uint64_t sortKeyBase = 0;

    for (const auto& entry : m_stages) {
        if (!entry.enabled) continue;

        const StageDesc& stage = entry.desc;

        switch (stage.type) {
            case StageType::Clear: {
                uint32_t mask = 0;
                if (stage.clearColor) mask |= GL_COLOR_BUFFER_BIT;
                if (stage.clearDepth) mask |= GL_DEPTH_BUFFER_BIT;

                if (stage.target.value != 0) {
                    buildBindRTCommand(work, stage.target, sortKeyBase);
                    sortKeyBase += 10;
                } else {
                    buildBindRTCommand(work, kNullRT, sortKeyBase);
                    sortKeyBase += 10;
                }

                buildClearCommand(work, mask,
                                  stage.clearColorValue.r / 255.0f,
                                  stage.clearColorValue.g / 255.0f,
                                  stage.clearColorValue.b / 255.0f,
                                  stage.clearColorValue.a / 255.0f,
                                  stage.clearDepthValue,
                                  0, sortKeyBase);
                sortKeyBase += 10;
                break;
            }

            case StageType::Geometry: {
                if (stage.target.value != 0) {
                    buildBindRTCommand(work, stage.target, sortKeyBase);
                    sortKeyBase += 10;
                }

                drawList.emitCommands(work,
                                      stage.sortMode,
                                      cam.position,
                                      stage.tagInclude,
                                      stage.tagExclude,
                                      stage.layer,
                                      stage.passName);
                sortKeyBase += 1000;
                break;
            }

            case StageType::Blit: {
                if (stage.target.value != 0) {
                    buildBindRTCommand(work, stage.target, sortKeyBase);
                    sortKeyBase += 10;
                } else {
                    buildBindRTCommand(work, kNullRT, sortKeyBase);
                    sortKeyBase += 10;
                }

                buildBlitCommand(work,
                                 stage.blitShader,
                                 stage.blitInputs.empty() ? nullptr : stage.blitInputs.data(),
                                 static_cast<int>(stage.blitInputs.size()),
                                 stage.target,
                                 &stage.blitUniforms,
                                 sortKeyBase);
                sortKeyBase += 10;
                break;
            }

            case StageType::Custom: {
                if (stage.customFn) {
                    stage.customFn(work);
                }
                sortKeyBase += 100;
                break;
            }
        }
    }
}

void Pipeline::emit(GameWork& work, const DrawList& drawList,
                    const Camera2D& cam, int screenW, int screenH) const {
    work.setScreenSize(screenW, screenH);

    Camera3D cam3D;
    cam3D.position = Vec3(cam.target.x, cam.target.y, cam.zoom);
    cam3D.target = Vec3(cam.target.x, cam.target.y, 0);
    cam3D.up = Vec3(0, 1, 0);
    cam3D.fov = 0;
    cam3D.near_clip = 0.1f;
    cam3D.far_clip = 1000.0f;
    cam3D.ortho = true;

    emit(work, drawList, cam3D, static_cast<float>(screenW) / screenH);
}

Pipeline MakeForwardPipeline(RTHandle screenRT) {
    Pipeline pipe;

    pipe.pushStage(StageDesc::clear("clear", screenRT, Colors::Black, 1.0f));
    pipe.pushStage(StageDesc::geometry("opaque", "FORWARD", SortMode::FrontToBack, screenRT));
    pipe.pushStage(StageDesc::geometry("transparent", "FORWARD", SortMode::BackToFront, screenRT));

    return pipe;
}

Pipeline MakeForwardHDRPipeline(RTHandle hdrRT, RTHandle screenRT,
                                ShaderHandle toneMapShader,
                                ShaderHandle bloomShader) {
    Pipeline pipe;

    pipe.pushStage(StageDesc::clear("clear_hdr", hdrRT, Colors::Black, 1.0f));
    pipe.pushStage(StageDesc::geometry("opaque", "FORWARD", SortMode::FrontToBack, hdrRT));
    pipe.pushStage(StageDesc::geometry("transparent", "FORWARD", SortMode::BackToFront, hdrRT));

    if (bloomShader.value != 0) {
        StageDesc bloomStage = StageDesc::blit("bloom", bloomShader, hdrRT);
        bloomStage.addBlitInput(kNullTex);
        pipe.pushStage(std::move(bloomStage));
    }

    StageDesc toneMapStage = StageDesc::blit("tonemap", toneMapShader, screenRT);
    toneMapStage.addBlitInput(kNullTex);
    pipe.pushStage(std::move(toneMapStage));

    return pipe;
}

} // namespace spt3d
