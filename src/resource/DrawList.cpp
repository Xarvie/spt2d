#include "DrawList.h"
#include "../core/GameWork.h"
#include "../core/RenderCommand.h"
#include "../render/Executor.h"

#include <algorithm>
#include <cmath>

namespace spt3d {

namespace {

Vec3 ExtractPositionFromMatrix(const Mat4& m) {
    return Vec3(m[3][0], m[3][1], m[3][2]);
}

void CopyMaterialUniforms(MaterialUniforms& out, const MaterialSnapshot& snap) {
    out.texCount = std::min(snap.texCount, MaterialUniforms::kMaxTextures);
    for (int i = 0; i < out.texCount; ++i) {
        out.textures[i].tex = snap.textures[i].tex;
        out.textures[i].nameHash = snap.textures[i].nameHash;
    }

    out.floatCount = std::min(snap.floatCount, MaterialUniforms::kMaxFloats);
    for (int i = 0; i < out.floatCount; ++i) {
        out.floats[i].nameHash = snap.floats[i].nameHash;
        out.floats[i].value = snap.floats[i].value;
    }

    out.vec4Count = std::min(snap.vec4Count, MaterialUniforms::kMaxVec4s);
    for (int i = 0; i < out.vec4Count; ++i) {
        out.vec4s[i].nameHash = snap.vec4s[i].nameHash;
        out.vec4s[i].value = snap.vec4s[i].value;
    }

    out.mat4Count = std::min(snap.mat4Count, MaterialUniforms::kMaxMat4s);
    for (int i = 0; i < out.mat4Count; ++i) {
        out.mat4s[i].nameHash = snap.mat4s[i].nameHash;
        out.mat4s[i].value = snap.mat4s[i].value;
    }

    out.state = snap.state;
}

}

void DrawList::sort(SortMode mode, Vec3 cameraPos) {
    if (m_items.empty()) return;

    if (mode == SortMode::None) return;

    std::vector<std::pair<float, int>> distances;
    distances.reserve(m_items.size());

    for (int i = 0; i < static_cast<int>(m_items.size()); ++i) {
        const DrawItem& item = m_items[i];
        Vec3 pos = ExtractPositionFromMatrix(item.transform);
        float dist = glm::length(pos - cameraPos);
        if (item.sortOverride > 0.0f) {
            dist = item.sortOverride;
        }
        distances.emplace_back(dist, i);
    }

    if (mode == SortMode::FrontToBack) {
        std::sort(distances.begin(), distances.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });
    } else {
        std::sort(distances.begin(), distances.end(),
            [](const auto& a, const auto& b) { return a.first > b.first; });
    }

    std::vector<DrawItem> sorted;
    sorted.reserve(m_items.size());
    for (const auto& p : distances) {
        sorted.push_back(m_items[p.second]);
    }
    m_items = std::move(sorted);
}

void DrawList::filterByTag(uint32_t includeHash, uint32_t excludeHash,
                           std::vector<const DrawItem*>& out) const {
    out.clear();
    out.reserve(m_items.size());

    for (const DrawItem& item : m_items) {
        uint32_t tagHash = item.material.tagHash;

        if (includeHash != 0 && tagHash != includeHash) {
            continue;
        }
        if (excludeHash != 0 && tagHash == excludeHash) {
            continue;
        }

        out.push_back(&item);
    }
}

void DrawList::emitCommands(GameWork& work,
                            SortMode sortMode,
                            Vec3 cameraPos,
                            uint32_t tagInclude,
                            uint32_t tagExclude,
                            uint8_t layer,
                            std::string_view passName) const {
    if (m_items.empty()) return;

    std::vector<int> indices;
    indices.reserve(m_items.size());

    for (int i = 0; i < static_cast<int>(m_items.size()); ++i) {
        const DrawItem& item = m_items[i];
        uint32_t tagHash = item.material.tagHash;

        if (tagInclude != 0 && tagHash != tagInclude) {
            continue;
        }
        if (tagExclude != 0 && tagHash == tagExclude) {
            continue;
        }

        indices.push_back(i);
    }

    if (indices.empty()) return;

    if (sortMode != SortMode::None) {
        std::vector<std::pair<float, int>> distances;
        distances.reserve(indices.size());

        for (int idx : indices) {
            const DrawItem& item = m_items[idx];
            Vec3 pos = ExtractPositionFromMatrix(item.transform);
            float dist = glm::length(pos - cameraPos);
            if (item.sortOverride > 0.0f) {
                dist = item.sortOverride;
            }
            distances.emplace_back(dist, idx);
        }

        if (sortMode == SortMode::FrontToBack) {
            std::sort(distances.begin(), distances.end(),
                [](const auto& a, const auto& b) { return a.first < b.first; });
        } else {
            std::sort(distances.begin(), distances.end(),
                [](const auto& a, const auto& b) { return a.first > b.first; });
        }

        indices.clear();
        for (const auto& p : distances) {
            indices.push_back(p.second);
        }
    }

    uint16_t seq = 0;
    for (int idx : indices) {
        const DrawItem& item = m_items[idx];

        MaterialUniforms uniforms;
        CopyMaterialUniforms(uniforms, item.material);

        uint32_t uniformsOffset = work.allocUniform(uniforms);
        uint32_t transformOffset = work.allocUniform(item.transform);

        if (uniformsOffset == UINT32_MAX || transformOffset == UINT32_MAX) {
            continue;
        }

        DrawMeshCmd cmd;
        cmd.mesh = item.mesh;
        cmd.shader = item.material.shader;
        cmd.uniformsOffset = uniformsOffset;
        cmd.transformOffset = transformOffset;

        Vec3 pos = ExtractPositionFromMatrix(item.transform);
        float dist = glm::length(pos - cameraPos);
        if (item.sortOverride > 0.0f) {
            dist = item.sortOverride;
        }

        uint32_t depth24 = static_cast<uint32_t>(
            std::min(std::max(dist * 1000.0f, 0.0f), 16777215.0f)
        );

        uint16_t matId = static_cast<uint16_t>(item.material.shader.value & 0xFFFF);

        uint64_t sortKey = RenderCommand::BuildSortKey(layer, depth24, matId, seq++);

        work.submit(cmd, cmd::execDrawMesh, sortKey, DrawMeshCmd::kTypeId);
    }
}

} // namespace spt3d
