// spt3d/resource/MeshData.h — CPU-side mesh data
// [THREAD: logic] — Built on the logic thread, consumed by GPUDevice on render thread.
//
// MeshData holds raw vertex/index arrays with NO GL objects. It is move-only
// (owns heap vectors). The logic thread builds it via MeshBuilder, then
// either:
//   a) Submits a ResourceCommand with the data embedded in uniformPool, or
//   b) Passes it directly to GPUDevice::createMesh() during pre-init.
//
// Vertex layout (interleaved, 16 floats per vertex):
//   [0..2]   position    (vec3)
//   [3..5]   normal      (vec3)
//   [6..7]   uv0         (vec2)
//   [8..11]  color       (vec4, normalized float)
//   [12..15] tangent     (vec4)
//
// Total stride = 16 * sizeof(float) = 64 bytes per vertex.
#pragma once

#include "../Types.h"
#include <vector>
#include <cstdint>
#include <memory>

namespace spt3d {

// =========================================================================
//  MeshData — the CPU-side payload
// =========================================================================

struct MeshData {
    std::vector<float>    vertices;     // interleaved, 16 floats/vertex
    std::vector<uint16_t> indices16;
    std::vector<uint32_t> indices32;

    int  vertexCount     = 0;
    int  indexCount       = 0;
    bool use32bitIndices  = false;
    bool dynamic          = false;
    AABB bounds           = {};

    /// Stride in floats per vertex (always 16).
    static constexpr int kFloatsPerVertex = 16;

    /// Stride in bytes per vertex.
    static constexpr int kBytesPerVertex = kFloatsPerVertex * sizeof(float);

    /// True if the mesh has index data.
    [[nodiscard]] bool isIndexed() const { return indexCount > 0; }

    /// Size of the vertex buffer in bytes.
    [[nodiscard]] std::size_t vertexBytes() const {
        return vertices.size() * sizeof(float);
    }

    /// Size of the index buffer in bytes.
    [[nodiscard]] std::size_t indexBytes() const {
        return use32bitIndices
            ? indices32.size() * sizeof(uint32_t)
            : indices16.size() * sizeof(uint16_t);
    }

    /// Pointer to index data (generic).
    [[nodiscard]] const void* indexData() const {
        return use32bitIndices
            ? static_cast<const void*>(indices32.data())
            : static_cast<const void*>(indices16.data());
    }
};

// =========================================================================
//  MeshBuilder — fluent builder producing MeshData (no GL calls)
// =========================================================================

class MeshBuilder {
public:
    MeshBuilder();
    ~MeshBuilder();

    MeshBuilder(MeshBuilder&&) noexcept;
    MeshBuilder& operator=(MeshBuilder&&) noexcept;
    MeshBuilder(const MeshBuilder&) = delete;
    MeshBuilder& operator=(const MeshBuilder&) = delete;

    MeshBuilder& Positions(const Vec3* data, int n);
    MeshBuilder& Normals(const Vec3* data, int n);
    MeshBuilder& Tangents(const Vec4* data, int n);
    MeshBuilder& UV0(const Vec2* data, int n);
    MeshBuilder& UV1(const Vec2* data, int n);
    MeshBuilder& VertColors(const Color* data, int n);
    MeshBuilder& BoneIdx(const uint8_t* data, int n);
    MeshBuilder& BoneWt(const Vec4* data, int n);
    MeshBuilder& Idx16(const uint16_t* data, int n);
    MeshBuilder& Idx32(const uint32_t* data, int n);
    MeshBuilder& Dynamic(bool d = true);

    /// Build the final MeshData. The builder is consumed (moved out).
    /// No GL calls — purely CPU work.
    MeshData Build();

private:
    struct Impl;
    std::unique_ptr<Impl> p;
};

// =========================================================================
//  Procedural mesh generators (return MeshData, no GL)
// =========================================================================

MeshData GenPlaneData(float w, float h, int sx = 1, int sz = 1);
MeshData GenCubeData(float w, float h, float d);
MeshData GenSphereData(float r, int rings = 16, int slices = 16);
MeshData GenHemiSphereData(float r, int rings = 16, int slices = 16);
MeshData GenCylinderData(float r, float h, int slices = 16);
MeshData GenConeData(float r, float h, int slices = 16);
MeshData GenTorusData(float r, float tube, int rseg = 24, int tseg = 12);
MeshData GenCapsuleData(float r, float h, int rings = 8, int slices = 16);
MeshData GenFullscreenTriData();
MeshData GenTriangleData(float size);

} // namespace spt3d
