// spt3d/resource/DrawList.h — Draw list for the logic thread
// [THREAD: logic] — Built on logic thread, then serialized into GameWork.
//
// DrawList collects (mesh handle, material snapshot, transform) tuples.
// During onRender(), the logic thread iterates DrawList and emits
// RenderCommands into GameWork. No GL calls, no shared_ptr.
//
// Usage:
//   DrawList dl;
//   dl.push({meshHandle, matSnapshot, modelMatrix});
//   dl.push({meshHandle, matSnapshot, transform});
//   // in onRender():
//   dl.emitCommands(work, cam);  // serializes into GameWork
#pragma once

#include "../Handle.h"
#include "../Types.h"
#include "MaterialSnapshot.h"

#include <vector>

namespace spt3d {

// Forward
struct GameWork;

// =========================================================================
//  DrawItem — a single renderable (value type, no GL)
// =========================================================================

struct DrawItem {
    MeshHandle       mesh;
    MaterialSnapshot material;
    Mat4             transform{1.0f};
    float            sortOverride = 0.0f;   // 0 = auto distance sort
};

// =========================================================================
//  DrawList
// =========================================================================

class DrawList {
public:
    DrawList() { m_items.reserve(256); }

    void clear() { m_items.clear(); }

    void push(const DrawItem& item) { m_items.push_back(item); }

    void push(MeshHandle mesh, const MaterialSnapshot& mat, Mat4 transform) {
        m_items.push_back({mesh, mat, transform, 0.0f});
    }

    void push(MeshHandle mesh, const MaterialSnapshot& mat, Transform transform) {
        m_items.push_back({mesh, mat, ToMat4(transform), 0.0f});
    }

    [[nodiscard]] int count() const { return static_cast<int>(m_items.size()); }
    [[nodiscard]] bool empty() const { return m_items.empty(); }
    [[nodiscard]] const std::vector<DrawItem>& items() const { return m_items; }
    [[nodiscard]]       std::vector<DrawItem>& items()       { return m_items; }

    /// Sort items by distance to camera (front-to-back or back-to-front).
    void sort(SortMode mode, Vec3 cameraPos);

    /// Filter items by tag hash. Fills `out` with pointers to matching items.
    void filterByTag(uint32_t includeHash, uint32_t excludeHash,
                     std::vector<const DrawItem*>& out) const;

    /// Serialize all items into GameWork as DrawMeshCmd render commands.
    /// This is the bridge between the logic-thread DrawList and the
    /// render-thread command stream. Called during IGameLogic::onRender().
    ///
    /// For each DrawItem:
    ///   1. Allocate MaterialUniforms in work.uniformPool (from MaterialSnapshot)
    ///   2. Allocate model Mat4 in work.uniformPool
    ///   3. Submit DrawMeshCmd with handle + pool offsets
    void emitCommands(GameWork& work,
                      SortMode sortMode = SortMode::FrontToBack,
                      Vec3 cameraPos = Vec3(0),
                      uint32_t tagInclude = 0,
                      uint32_t tagExclude = 0,
                      uint8_t layer = 128,
                      std::string_view passName = "FORWARD") const;

private:
    std::vector<DrawItem> m_items;
};

} // namespace spt3d
