#include "MeshData.h"
#include <cmath>
#include <algorithm>
#include <cstring>

namespace spt3d {

static constexpr float PI = 3.14159265358979323846f;

struct MeshBuilder::Impl {
    std::vector<Vec3>   positions;
    std::vector<Vec3>   normals;
    std::vector<Vec2>   uv0;
    std::vector<Vec2>   uv1;
    std::vector<Vec4>   tangents;
    std::vector<Color>  colors;
    std::vector<uint8_t>boneIdx;
    std::vector<Vec4>   boneWt;
    std::vector<uint16_t> indices16;
    std::vector<uint32_t> indices32;
    bool use32bit = false;
    bool dynamic = false;
    int vertexCount = 0;
};

MeshBuilder::MeshBuilder() : p(std::make_unique<Impl>()) {}
MeshBuilder::~MeshBuilder() = default;
MeshBuilder::MeshBuilder(MeshBuilder&&) noexcept = default;
MeshBuilder& MeshBuilder::operator=(MeshBuilder&&) noexcept = default;

MeshBuilder& MeshBuilder::Positions(const Vec3* data, int n) {
    p->positions.assign(data, data + n);
    p->vertexCount = n;
    return *this;
}

MeshBuilder& MeshBuilder::Normals(const Vec3* data, int n) {
    p->normals.assign(data, data + n);
    return *this;
}

MeshBuilder& MeshBuilder::Tangents(const Vec4* data, int n) {
    p->tangents.assign(data, data + n);
    return *this;
}

MeshBuilder& MeshBuilder::UV0(const Vec2* data, int n) {
    p->uv0.assign(data, data + n);
    return *this;
}

MeshBuilder& MeshBuilder::UV1(const Vec2* data, int n) {
    p->uv1.assign(data, data + n);
    return *this;
}

MeshBuilder& MeshBuilder::VertColors(const Color* data, int n) {
    p->colors.assign(data, data + n);
    return *this;
}

MeshBuilder& MeshBuilder::BoneIdx(const uint8_t* data, int n) {
    p->boneIdx.assign(data, data + n);
    return *this;
}

MeshBuilder& MeshBuilder::BoneWt(const Vec4* data, int n) {
    p->boneWt.assign(data, data + n);
    return *this;
}

MeshBuilder& MeshBuilder::Idx16(const uint16_t* data, int n) {
    p->indices16.assign(data, data + n);
    p->use32bit = false;
    return *this;
}

MeshBuilder& MeshBuilder::Idx32(const uint32_t* data, int n) {
    p->indices32.assign(data, data + n);
    p->use32bit = true;
    return *this;
}

MeshBuilder& MeshBuilder::Dynamic(bool d) {
    p->dynamic = d;
    return *this;
}

MeshData MeshBuilder::Build() {
    MeshData data;
    const int n = p->vertexCount;
    if (n <= 0) return data;

    data.vertexCount = n;
    data.dynamic = p->dynamic;
    data.vertices.resize(static_cast<size_t>(n) * MeshData::kFloatsPerVertex);

    const bool hasNormal   = !p->normals.empty();
    const bool hasUV0      = !p->uv0.empty();
    const bool hasTangent  = !p->tangents.empty();
    const bool hasColor    = !p->colors.empty();

    float* dst = data.vertices.data();
    for (int i = 0; i < n; ++i) {
        float* v = dst + i * MeshData::kFloatsPerVertex;

        Vec3 pos = (i < static_cast<int>(p->positions.size())) ? p->positions[i] : Vec3(0);
        v[0] = pos.x; v[1] = pos.y; v[2] = pos.z;

        Vec3 norm = hasNormal ? p->normals[i] : Vec3(0, 1, 0);
        v[3] = norm.x; v[4] = norm.y; v[5] = norm.z;

        Vec2 uv = hasUV0 ? p->uv0[i] : Vec2(0);
        v[6] = uv.x; v[7] = uv.y;

        Vec4 col = hasColor ? ToVec4(p->colors[i]) : Vec4(1);
        v[8] = col.r; v[9] = col.g; v[10] = col.b; v[11] = col.a;

        Vec4 tan = hasTangent ? p->tangents[i] : Vec4(1, 0, 0, 1);
        v[12] = tan.x; v[13] = tan.y; v[14] = tan.z; v[15] = tan.w;
    }

    if (p->use32bit) {
        data.indices32 = std::move(p->indices32);
        data.indexCount = static_cast<int>(data.indices32.size());
        data.use32bitIndices = true;
    } else {
        data.indices16 = std::move(p->indices16);
        data.indexCount = static_cast<int>(data.indices16.size());
        data.use32bitIndices = false;
    }

    data.bounds.min = Vec3(FLT_MAX);
    data.bounds.max = Vec3(-FLT_MAX);
    for (int i = 0; i < n; ++i) {
        const Vec3& pos = p->positions[i];
        data.bounds.min = glm::min(data.bounds.min, pos);
        data.bounds.max = glm::max(data.bounds.max, pos);
    }

    return data;
}

static void computeTangentBasis(Vec3* tangents, Vec3* bitangents,
                                const Vec3* positions, const Vec2* uvs,
                                const uint16_t* indices, int indexCount) {
    for (int i = 0; i < indexCount; i += 3) {
        uint16_t i0 = indices[i];
        uint16_t i1 = indices[i + 1];
        uint16_t i2 = indices[i + 2];

        Vec3 v0 = positions[i0], v1 = positions[i1], v2 = positions[i2];
        Vec2 uv0 = uvs[i0], uv1 = uvs[i1], uv2 = uvs[i2];

        Vec3 edge1 = v1 - v0;
        Vec3 edge2 = v2 - v0;
        Vec2 duv1 = uv1 - uv0;
        Vec2 duv2 = uv2 - uv0;

        float det = duv1.x * duv2.y - duv2.x * duv1.y;
        if (std::abs(det) < 1e-6f) continue;

        float r = 1.0f / det;
        Vec3 tangent = (edge1 * duv2.y - edge2 * duv1.y) * r;
        Vec3 bitangent = (edge2 * duv1.x - edge1 * duv2.x) * r;

        tangents[i0] += tangent;
        tangents[i1] += tangent;
        tangents[i2] += tangent;
        bitangents[i0] += bitangent;
        bitangents[i1] += bitangent;
        bitangents[i2] += bitangent;
    }
}

MeshData GenPlaneData(float w, float h, int sx, int sz) {
    MeshData data;
    const int vertsX = sx + 1;
    const int vertsZ = sz + 1;
    const int vertexCount = vertsX * vertsZ;
    const int indexCount = sx * sz * 6;

    data.vertexCount = vertexCount;
    data.vertices.resize(static_cast<size_t>(vertexCount) * MeshData::kFloatsPerVertex);

    const float halfW = w * 0.5f;
    const float halfH = h * 0.5f;
    const float stepX = w / sx;
    const float stepZ = h / sz;

    for (int iz = 0; iz < vertsZ; ++iz) {
        for (int ix = 0; ix < vertsX; ++ix) {
            int i = iz * vertsX + ix;
            float* v = data.vertices.data() + i * MeshData::kFloatsPerVertex;

            float x = -halfW + ix * stepX;
            float z = -halfH + iz * stepZ;

            v[0] = x; v[1] = 0; v[2] = z;
            v[3] = 0; v[4] = 1; v[5] = 0;
            v[6] = static_cast<float>(ix) / sx; v[7] = static_cast<float>(iz) / sz;
            v[8] = 1; v[9] = 1; v[10] = 1; v[11] = 1;
            v[12] = 1; v[13] = 0; v[14] = 0; v[15] = 1;
        }
    }

    data.indices16.resize(indexCount);
    data.indexCount = indexCount;
    int idx = 0;
    for (int iz = 0; iz < sz; ++iz) {
        for (int ix = 0; ix < sx; ++ix) {
            int v0 = iz * vertsX + ix;
            int v1 = v0 + 1;
            int v2 = v0 + vertsX;
            int v3 = v2 + 1;

            data.indices16[idx++] = static_cast<uint16_t>(v0);
            data.indices16[idx++] = static_cast<uint16_t>(v2);
            data.indices16[idx++] = static_cast<uint16_t>(v1);
            data.indices16[idx++] = static_cast<uint16_t>(v1);
            data.indices16[idx++] = static_cast<uint16_t>(v2);
            data.indices16[idx++] = static_cast<uint16_t>(v3);
        }
    }

    data.bounds.min = Vec3(-halfW, 0, -halfH);
    data.bounds.max = Vec3(halfW, 0, halfH);

    return data;
}

MeshData GenCubeData(float w, float h, float d) {
    MeshData data;
    const int vertexCount = 24;
    const int indexCount = 36;

    data.vertexCount = vertexCount;
    data.vertices.resize(static_cast<size_t>(vertexCount) * MeshData::kFloatsPerVertex);

    const float hw = w * 0.5f, hh = h * 0.5f, hd = d * 0.5f;

    struct FaceData {
        Vec3 normal; Vec3 tangent; int axis; float sign;
    };
    FaceData faces[6] = {
        {{ 0,  0,  1}, { 1, 0, 0}, 2,  1},
        {{ 0,  0, -1}, {-1, 0, 0}, 2, -1},
        {{ 0,  1,  0}, { 1, 0, 0}, 1,  1},
        {{ 0, -1,  0}, { 1, 0, 0}, 1, -1},
        {{ 1,  0,  0}, { 0, 0, -1}, 0,  1},
        {{-1,  0,  0}, { 0, 0,  1}, 0, -1},
    };

    int v = 0;
    for (int f = 0; f < 6; ++f) {
        const auto& face = faces[f];
        float* dst = data.vertices.data() + v * MeshData::kFloatsPerVertex;

        float x0 = -hw, x1 = hw, y0 = -hh, y1 = hh, z0 = -hd, z1 = hd;
        float u0 = 0, u1 = 1, v0 = 0, v1 = 1;

        Vec3 p[4];
        Vec2 uv[4] = {{u0,v0},{u1,v0},{u1,v1},{u0,v1}};

        switch (f) {
            case 0: p[0]={x0,y0,z1}; p[1]={x1,y0,z1}; p[2]={x1,y1,z1}; p[3]={x0,y1,z1}; break;
            case 1: p[0]={x1,y0,z0}; p[1]={x0,y0,z0}; p[2]={x0,y1,z0}; p[3]={x1,y1,z0}; break;
            case 2: p[0]={x0,y1,z1}; p[1]={x1,y1,z1}; p[2]={x1,y1,z0}; p[3]={x0,y1,z0}; break;
            case 3: p[0]={x0,y0,z0}; p[1]={x1,y0,z0}; p[2]={x1,y0,z1}; p[3]={x0,y0,z1}; break;
            case 4: p[0]={x1,y0,z1}; p[1]={x1,y0,z0}; p[2]={x1,y1,z0}; p[3]={x1,y1,z1}; break;
            case 5: p[0]={x0,y0,z0}; p[1]={x0,y0,z1}; p[2]={x0,y1,z1}; p[3]={x0,y1,z0}; break;
        }

        for (int i = 0; i < 4; ++i) {
            float* vert = dst + i * MeshData::kFloatsPerVertex;
            vert[0] = p[i].x; vert[1] = p[i].y; vert[2] = p[i].z;
            vert[3] = face.normal.x; vert[4] = face.normal.y; vert[5] = face.normal.z;
            vert[6] = uv[i].x; vert[7] = uv[i].y;
            vert[8] = 1; vert[9] = 1; vert[10] = 1; vert[11] = 1;
            vert[12] = face.tangent.x; vert[13] = face.tangent.y;
            vert[14] = face.tangent.z; vert[15] = 1;
        }
        v += 4;
    }

    data.indices16.resize(indexCount);
    data.indexCount = indexCount;
    uint16_t quadIdx[] = {0,1,2,0,2,3};
    for (int f = 0; f < 6; ++f) {
        int base = f * 4;
        for (int i = 0; i < 6; ++i) {
            data.indices16[f * 6 + i] = static_cast<uint16_t>(base + quadIdx[i]);
        }
    }

    data.bounds.min = Vec3(-hw, -hh, -hd);
    data.bounds.max = Vec3(hw, hh, hd);

    return data;
}

MeshData GenSphereData(float r, int rings, int slices) {
    MeshData data;
    const int vertexCount = (rings + 1) * (slices + 1);
    const int indexCount = rings * slices * 6;

    data.vertexCount = vertexCount;
    data.vertices.resize(static_cast<size_t>(vertexCount) * MeshData::kFloatsPerVertex);

    int v = 0;
    for (int ring = 0; ring <= rings; ++ring) {
        float phi = PI * ring / rings;
        float y = std::cos(phi);
        float ringRadius = std::sin(phi);

        for (int slice = 0; slice <= slices; ++slice) {
            float theta = 2.0f * PI * slice / slices;
            float x = ringRadius * std::cos(theta);
            float z = ringRadius * std::sin(theta);

            float* vert = data.vertices.data() + v * MeshData::kFloatsPerVertex;
            vert[0] = x * r; vert[1] = y * r; vert[2] = z * r;
            vert[3] = x; vert[4] = y; vert[5] = z;
            vert[6] = static_cast<float>(slice) / slices;
            vert[7] = static_cast<float>(ring) / rings;
            vert[8] = 1; vert[9] = 1; vert[10] = 1; vert[11] = 1;

            Vec3 tangent = glm::normalize(Vec3(-z, 0, x));
            vert[12] = tangent.x; vert[13] = tangent.y; vert[14] = tangent.z; vert[15] = 1;
            ++v;
        }
    }

    data.indices16.resize(indexCount);
    data.indexCount = indexCount;
    int idx = 0;
    for (int ring = 0; ring < rings; ++ring) {
        for (int slice = 0; slice < slices; ++slice) {
            int v0 = ring * (slices + 1) + slice;
            int v1 = v0 + 1;
            int v2 = v0 + slices + 1;
            int v3 = v2 + 1;

            data.indices16[idx++] = static_cast<uint16_t>(v0);
            data.indices16[idx++] = static_cast<uint16_t>(v2);
            data.indices16[idx++] = static_cast<uint16_t>(v1);
            data.indices16[idx++] = static_cast<uint16_t>(v1);
            data.indices16[idx++] = static_cast<uint16_t>(v2);
            data.indices16[idx++] = static_cast<uint16_t>(v3);
        }
    }

    data.bounds.min = Vec3(-r, -r, -r);
    data.bounds.max = Vec3(r, r, r);

    return data;
}

MeshData GenHemiSphereData(float r, int rings, int slices) {
    MeshData data;
    const int vertexCount = (rings + 1) * (slices + 1);
    const int indexCount = rings * slices * 6;

    data.vertexCount = vertexCount;
    data.vertices.resize(static_cast<size_t>(vertexCount) * MeshData::kFloatsPerVertex);

    int v = 0;
    for (int ring = 0; ring <= rings; ++ring) {
        float phi = 0.5f * PI * ring / rings;
        float y = std::cos(phi);
        float ringRadius = std::sin(phi);

        for (int slice = 0; slice <= slices; ++slice) {
            float theta = 2.0f * PI * slice / slices;
            float x = ringRadius * std::cos(theta);
            float z = ringRadius * std::sin(theta);

            float* vert = data.vertices.data() + v * MeshData::kFloatsPerVertex;
            vert[0] = x * r; vert[1] = y * r; vert[2] = z * r;
            vert[3] = x; vert[4] = y; vert[5] = z;
            vert[6] = static_cast<float>(slice) / slices;
            vert[7] = static_cast<float>(ring) / rings;
            vert[8] = 1; vert[9] = 1; vert[10] = 1; vert[11] = 1;

            Vec3 tangent = glm::normalize(Vec3(-z, 0, x));
            vert[12] = tangent.x; vert[13] = tangent.y; vert[14] = tangent.z; vert[15] = 1;
            ++v;
        }
    }

    data.indices16.resize(indexCount);
    data.indexCount = indexCount;
    int idx = 0;
    for (int ring = 0; ring < rings; ++ring) {
        for (int slice = 0; slice < slices; ++slice) {
            int v0 = ring * (slices + 1) + slice;
            int v1 = v0 + 1;
            int v2 = v0 + slices + 1;
            int v3 = v2 + 1;

            data.indices16[idx++] = static_cast<uint16_t>(v0);
            data.indices16[idx++] = static_cast<uint16_t>(v2);
            data.indices16[idx++] = static_cast<uint16_t>(v1);
            data.indices16[idx++] = static_cast<uint16_t>(v1);
            data.indices16[idx++] = static_cast<uint16_t>(v2);
            data.indices16[idx++] = static_cast<uint16_t>(v3);
        }
    }

    data.bounds.min = Vec3(-r, 0, -r);
    data.bounds.max = Vec3(r, r, r);

    return data;
}

MeshData GenCylinderData(float r, float h, int slices) {
    MeshData data;
    const int rings = 1;
    const int vertexCount = (rings + 3) * (slices + 1);
    const int indexCount = rings * slices * 6 + slices * 6;

    data.vertexCount = vertexCount;
    data.vertices.resize(static_cast<size_t>(vertexCount) * MeshData::kFloatsPerVertex);

    const float halfH = h * 0.5f;
    int v = 0;

    for (int slice = 0; slice <= slices; ++slice) {
        float theta = 2.0f * PI * slice / slices;
        float x = std::cos(theta);
        float z = std::sin(theta);

        float* vert = data.vertices.data() + v * MeshData::kFloatsPerVertex;
        vert[0] = x * r; vert[1] = -halfH; vert[2] = z * r;
        vert[3] = x; vert[4] = 0; vert[5] = z;
        vert[6] = static_cast<float>(slice) / slices; vert[7] = 0;
        vert[8] = 1; vert[9] = 1; vert[10] = 1; vert[11] = 1;
        vert[12] = -z; vert[13] = 0; vert[14] = x; vert[15] = 1;
        ++v;
    }

    for (int slice = 0; slice <= slices; ++slice) {
        float theta = 2.0f * PI * slice / slices;
        float x = std::cos(theta);
        float z = std::sin(theta);

        float* vert = data.vertices.data() + v * MeshData::kFloatsPerVertex;
        vert[0] = x * r; vert[1] = halfH; vert[2] = z * r;
        vert[3] = x; vert[4] = 0; vert[5] = z;
        vert[6] = static_cast<float>(slice) / slices; vert[7] = 1;
        vert[8] = 1; vert[9] = 1; vert[10] = 1; vert[11] = 1;
        vert[12] = -z; vert[13] = 0; vert[14] = x; vert[15] = 1;
        ++v;
    }

    for (int slice = 0; slice <= slices; ++slice) {
        float theta = 2.0f * PI * slice / slices;
        float x = std::cos(theta);
        float z = std::sin(theta);

        float* vert = data.vertices.data() + v * MeshData::kFloatsPerVertex;
        vert[0] = x * r; vert[1] = -halfH; vert[2] = z * r;
        vert[3] = 0; vert[4] = -1; vert[5] = 0;
        vert[6] = static_cast<float>(slice) / slices; vert[7] = 0;
        vert[8] = 1; vert[9] = 1; vert[10] = 1; vert[11] = 1;
        vert[12] = 1; vert[13] = 0; vert[14] = 0; vert[15] = 1;
        ++v;
    }

    for (int slice = 0; slice <= slices; ++slice) {
        float theta = 2.0f * PI * slice / slices;
        float x = std::cos(theta);
        float z = std::sin(theta);

        float* vert = data.vertices.data() + v * MeshData::kFloatsPerVertex;
        vert[0] = x * r; vert[1] = halfH; vert[2] = z * r;
        vert[3] = 0; vert[4] = 1; vert[5] = 0;
        vert[6] = static_cast<float>(slice) / slices; vert[7] = 1;
        vert[8] = 1; vert[9] = 1; vert[10] = 1; vert[11] = 1;
        vert[12] = 1; vert[13] = 0; vert[14] = 0; vert[15] = 1;
        ++v;
    }

    data.indices16.resize(indexCount);
    data.indexCount = indexCount;
    int idx = 0;

    for (int slice = 0; slice < slices; ++slice) {
        int v0 = slice;
        int v1 = slice + 1;
        int v2 = slice + slices + 1;
        int v3 = v2 + 1;

        data.indices16[idx++] = static_cast<uint16_t>(v0);
        data.indices16[idx++] = static_cast<uint16_t>(v2);
        data.indices16[idx++] = static_cast<uint16_t>(v1);
        data.indices16[idx++] = static_cast<uint16_t>(v1);
        data.indices16[idx++] = static_cast<uint16_t>(v2);
        data.indices16[idx++] = static_cast<uint16_t>(v3);
    }

    int bottomBase = 2 * (slices + 1);
    int topBase = 3 * (slices + 1);
    for (int slice = 0; slice < slices; ++slice) {
        data.indices16[idx++] = static_cast<uint16_t>(bottomBase);
        data.indices16[idx++] = static_cast<uint16_t>(bottomBase + slice + 1);
        data.indices16[idx++] = static_cast<uint16_t>(bottomBase + slice);

        data.indices16[idx++] = static_cast<uint16_t>(topBase);
        data.indices16[idx++] = static_cast<uint16_t>(topBase + slice);
        data.indices16[idx++] = static_cast<uint16_t>(topBase + slice + 1);
    }

    data.bounds.min = Vec3(-r, -halfH, -r);
    data.bounds.max = Vec3(r, halfH, r);

    return data;
}

MeshData GenConeData(float r, float h, int slices) {
    MeshData data;
    const int vertexCount = 2 * (slices + 1) + 1;
    const int indexCount = slices * 3 + slices * 3;

    data.vertexCount = vertexCount;
    data.vertices.resize(static_cast<size_t>(vertexCount) * MeshData::kFloatsPerVertex);

    const float halfH = h * 0.5f;
    int v = 0;

    for (int slice = 0; slice <= slices; ++slice) {
        float theta = 2.0f * PI * slice / slices;
        float x = std::cos(theta);
        float z = std::sin(theta);

        float* vert = data.vertices.data() + v * MeshData::kFloatsPerVertex;
        vert[0] = x * r; vert[1] = -halfH; vert[2] = z * r;
        vert[3] = x; vert[4] = 0; vert[5] = z;
        vert[6] = static_cast<float>(slice) / slices; vert[7] = 0;
        vert[8] = 1; vert[9] = 1; vert[10] = 1; vert[11] = 1;
        vert[12] = -z; vert[13] = 0; vert[14] = x; vert[15] = 1;
        ++v;
    }

    for (int slice = 0; slice <= slices; ++slice) {
        float theta = 2.0f * PI * slice / slices;
        float x = std::cos(theta);
        float z = std::sin(theta);

        float* vert = data.vertices.data() + v * MeshData::kFloatsPerVertex;
        vert[0] = 0; vert[1] = halfH; vert[2] = 0;
        vert[3] = x; vert[4] = 0; vert[5] = z;
        vert[6] = static_cast<float>(slice) / slices; vert[7] = 1;
        vert[8] = 1; vert[9] = 1; vert[10] = 1; vert[11] = 1;
        vert[12] = -z; vert[13] = 0; vert[14] = x; vert[15] = 1;
        ++v;
    }

    {
        float* vert = data.vertices.data() + v * MeshData::kFloatsPerVertex;
        vert[0] = 0; vert[1] = -halfH; vert[2] = 0;
        vert[3] = 0; vert[4] = -1; vert[5] = 0;
        vert[6] = 0.5f; vert[7] = 0.5f;
        vert[8] = 1; vert[9] = 1; vert[10] = 1; vert[11] = 1;
        vert[12] = 1; vert[13] = 0; vert[14] = 0; vert[15] = 1;
        ++v;
    }

    data.indices16.resize(indexCount);
    data.indexCount = indexCount;
    int idx = 0;

    int topBase = slices + 1;
    for (int slice = 0; slice < slices; ++slice) {
        data.indices16[idx++] = static_cast<uint16_t>(slice);
        data.indices16[idx++] = static_cast<uint16_t>(topBase + slice);
        data.indices16[idx++] = static_cast<uint16_t>(slice + 1);
    }

    int bottomCenter = 2 * (slices + 1);
    for (int slice = 0; slice < slices; ++slice) {
        data.indices16[idx++] = static_cast<uint16_t>(bottomCenter);
        data.indices16[idx++] = static_cast<uint16_t>(slice + 1);
        data.indices16[idx++] = static_cast<uint16_t>(slice);
    }

    data.bounds.min = Vec3(-r, -halfH, -r);
    data.bounds.max = Vec3(r, halfH, r);

    return data;
}

MeshData GenTorusData(float r, float tube, int rseg, int tseg) {
    MeshData data;
    const int vertexCount = (rseg + 1) * (tseg + 1);
    const int indexCount = rseg * tseg * 6;

    data.vertexCount = vertexCount;
    data.vertices.resize(static_cast<size_t>(vertexCount) * MeshData::kFloatsPerVertex);

    int v = 0;
    for (int i = 0; i <= rseg; ++i) {
        float u = 2.0f * PI * i / rseg;
        float cosU = std::cos(u);
        float sinU = std::sin(u);

        for (int j = 0; j <= tseg; ++j) {
            float vAngle = 2.0f * PI * j / tseg;
            float cosV = std::cos(vAngle);
            float sinV = std::sin(vAngle);

            float x = (r + tube * cosV) * cosU;
            float y = tube * sinV;
            float z = (r + tube * cosV) * sinU;

            float nx = cosV * cosU;
            float ny = sinV;
            float nz = cosV * sinU;

            float* vert = data.vertices.data() + v * MeshData::kFloatsPerVertex;
            vert[0] = x; vert[1] = y; vert[2] = z;
            vert[3] = nx; vert[4] = ny; vert[5] = nz;
            vert[6] = static_cast<float>(i) / rseg;
            vert[7] = static_cast<float>(j) / tseg;
            vert[8] = 1; vert[9] = 1; vert[10] = 1; vert[11] = 1;

            Vec3 tangent = glm::normalize(Vec3(-sinU, 0, cosU));
            vert[12] = tangent.x; vert[13] = tangent.y; vert[14] = tangent.z; vert[15] = 1;
            ++v;
        }
    }

    data.indices16.resize(indexCount);
    data.indexCount = indexCount;
    int idx = 0;
    for (int i = 0; i < rseg; ++i) {
        for (int j = 0; j < tseg; ++j) {
            int v0 = i * (tseg + 1) + j;
            int v1 = v0 + 1;
            int v2 = v0 + tseg + 1;
            int v3 = v2 + 1;

            data.indices16[idx++] = static_cast<uint16_t>(v0);
            data.indices16[idx++] = static_cast<uint16_t>(v2);
            data.indices16[idx++] = static_cast<uint16_t>(v1);
            data.indices16[idx++] = static_cast<uint16_t>(v1);
            data.indices16[idx++] = static_cast<uint16_t>(v2);
            data.indices16[idx++] = static_cast<uint16_t>(v3);
        }
    }

    data.bounds.min = Vec3(-r - tube, -tube, -r - tube);
    data.bounds.max = Vec3(r + tube, tube, r + tube);

    return data;
}

MeshData GenCapsuleData(float r, float h, int rings, int slices) {
    MeshData data;
    const float cylinderH = h - 2 * r;
    const int hemiRings = rings / 2;

    const int topVertexCount = (hemiRings + 1) * (slices + 1);
    const int cylVertexCount = 2 * (slices + 1);
    const int bottomVertexCount = (hemiRings + 1) * (slices + 1);
    const int vertexCount = topVertexCount + cylVertexCount + bottomVertexCount;

    const int topIndexCount = hemiRings * slices * 6;
    const int cylIndexCount = slices * 6;
    const int bottomIndexCount = hemiRings * slices * 6;
    const int indexCount = topIndexCount + cylIndexCount + bottomIndexCount;

    data.vertexCount = vertexCount;
    data.vertices.resize(static_cast<size_t>(vertexCount) * MeshData::kFloatsPerVertex);

    const float halfCylH = cylinderH * 0.5f;
    int v = 0;

    for (int ring = 0; ring <= hemiRings; ++ring) {
        float phi = 0.5f * PI * ring / hemiRings;
        float y = std::cos(phi);
        float ringRadius = std::sin(phi);

        for (int slice = 0; slice <= slices; ++slice) {
            float theta = 2.0f * PI * slice / slices;
            float x = ringRadius * std::cos(theta);
            float z = ringRadius * std::sin(theta);

            float* vert = data.vertices.data() + v * MeshData::kFloatsPerVertex;
            vert[0] = x * r; vert[1] = y * r + halfCylH + r; vert[2] = z * r;
            vert[3] = x; vert[4] = y; vert[5] = z;
            vert[6] = static_cast<float>(slice) / slices;
            vert[7] = static_cast<float>(ring) / hemiRings;
            vert[8] = 1; vert[9] = 1; vert[10] = 1; vert[11] = 1;

            Vec3 tangent = glm::normalize(Vec3(-z, 0, x));
            vert[12] = tangent.x; vert[13] = tangent.y; vert[14] = tangent.z; vert[15] = 1;
            ++v;
        }
    }

    for (int slice = 0; slice <= slices; ++slice) {
        float theta = 2.0f * PI * slice / slices;
        float x = std::cos(theta);
        float z = std::sin(theta);

        float* vert = data.vertices.data() + v * MeshData::kFloatsPerVertex;
        vert[0] = x * r; vert[1] = -halfCylH; vert[2] = z * r;
        vert[3] = x; vert[4] = 0; vert[5] = z;
        vert[6] = static_cast<float>(slice) / slices; vert[7] = 0;
        vert[8] = 1; vert[9] = 1; vert[10] = 1; vert[11] = 1;
        vert[12] = -z; vert[13] = 0; vert[14] = x; vert[15] = 1;
        ++v;
    }

    for (int slice = 0; slice <= slices; ++slice) {
        float theta = 2.0f * PI * slice / slices;
        float x = std::cos(theta);
        float z = std::sin(theta);

        float* vert = data.vertices.data() + v * MeshData::kFloatsPerVertex;
        vert[0] = x * r; vert[1] = halfCylH; vert[2] = z * r;
        vert[3] = x; vert[4] = 0; vert[5] = z;
        vert[6] = static_cast<float>(slice) / slices; vert[7] = 1;
        vert[8] = 1; vert[9] = 1; vert[10] = 1; vert[11] = 1;
        vert[12] = -z; vert[13] = 0; vert[14] = x; vert[15] = 1;
        ++v;
    }

    for (int ring = 0; ring <= hemiRings; ++ring) {
        float phi = 0.5f * PI + 0.5f * PI * ring / hemiRings;
        float y = std::cos(phi);
        float ringRadius = std::sin(phi);

        for (int slice = 0; slice <= slices; ++slice) {
            float theta = 2.0f * PI * slice / slices;
            float x = ringRadius * std::cos(theta);
            float z = ringRadius * std::sin(theta);

            float* vert = data.vertices.data() + v * MeshData::kFloatsPerVertex;
            vert[0] = x * r; vert[1] = y * r - halfCylH - r; vert[2] = z * r;
            vert[3] = x; vert[4] = y; vert[5] = z;
            vert[6] = static_cast<float>(slice) / slices;
            vert[7] = static_cast<float>(ring) / hemiRings;
            vert[8] = 1; vert[9] = 1; vert[10] = 1; vert[11] = 1;

            Vec3 tangent = glm::normalize(Vec3(-z, 0, x));
            vert[12] = tangent.x; vert[13] = tangent.y; vert[14] = tangent.z; vert[15] = 1;
            ++v;
        }
    }

    data.indices16.resize(indexCount);
    data.indexCount = indexCount;
    int idx = 0;

    for (int ring = 0; ring < hemiRings; ++ring) {
        for (int slice = 0; slice < slices; ++slice) {
            int v0 = ring * (slices + 1) + slice;
            int v1 = v0 + 1;
            int v2 = v0 + slices + 1;
            int v3 = v2 + 1;

            data.indices16[idx++] = static_cast<uint16_t>(v0);
            data.indices16[idx++] = static_cast<uint16_t>(v2);
            data.indices16[idx++] = static_cast<uint16_t>(v1);
            data.indices16[idx++] = static_cast<uint16_t>(v1);
            data.indices16[idx++] = static_cast<uint16_t>(v2);
            data.indices16[idx++] = static_cast<uint16_t>(v3);
        }
    }

    int cylBottomBase = topVertexCount;
    int cylTopBase = topVertexCount + slices + 1;
    for (int slice = 0; slice < slices; ++slice) {
        int v0 = cylBottomBase + slice;
        int v1 = v0 + 1;
        int v2 = cylTopBase + slice;
        int v3 = v2 + 1;

        data.indices16[idx++] = static_cast<uint16_t>(v0);
        data.indices16[idx++] = static_cast<uint16_t>(v2);
        data.indices16[idx++] = static_cast<uint16_t>(v1);
        data.indices16[idx++] = static_cast<uint16_t>(v1);
        data.indices16[idx++] = static_cast<uint16_t>(v2);
        data.indices16[idx++] = static_cast<uint16_t>(v3);
    }

    int bottomBase = topVertexCount + cylVertexCount;
    for (int ring = 0; ring < hemiRings; ++ring) {
        for (int slice = 0; slice < slices; ++slice) {
            int v0 = bottomBase + ring * (slices + 1) + slice;
            int v1 = v0 + 1;
            int v2 = v0 + slices + 1;
            int v3 = v2 + 1;

            data.indices16[idx++] = static_cast<uint16_t>(v0);
            data.indices16[idx++] = static_cast<uint16_t>(v2);
            data.indices16[idx++] = static_cast<uint16_t>(v1);
            data.indices16[idx++] = static_cast<uint16_t>(v1);
            data.indices16[idx++] = static_cast<uint16_t>(v2);
            data.indices16[idx++] = static_cast<uint16_t>(v3);
        }
    }

    const float totalH = cylinderH + 2 * r;
    data.bounds.min = Vec3(-r, -totalH * 0.5f, -r);
    data.bounds.max = Vec3(r, totalH * 0.5f, r);

    return data;
}

MeshData GenFullscreenTriData() {
    MeshData data;
    data.vertexCount = 3;
    data.vertices.resize(3 * MeshData::kFloatsPerVertex);

    float* v0 = data.vertices.data();
    float* v1 = v0 + MeshData::kFloatsPerVertex;
    float* v2 = v1 + MeshData::kFloatsPerVertex;

    v0[0] = -1; v0[1] = -1; v0[2] = 0;
    v0[3] = 0; v0[4] = 0; v0[5] = 1;
    v0[6] = 0; v0[7] = 0;
    v0[8] = 1; v0[9] = 1; v0[10] = 1; v0[11] = 1;
    v0[12] = 1; v0[13] = 0; v0[14] = 0; v0[15] = 1;

    v1[0] = 3; v1[1] = -1; v1[2] = 0;
    v1[3] = 0; v1[4] = 0; v1[5] = 1;
    v1[6] = 2; v1[7] = 0;
    v1[8] = 1; v1[9] = 1; v1[10] = 1; v1[11] = 1;
    v1[12] = 1; v1[13] = 0; v1[14] = 0; v1[15] = 1;

    v2[0] = -1; v2[1] = 3; v2[2] = 0;
    v2[3] = 0; v2[4] = 0; v2[5] = 1;
    v2[6] = 0; v2[7] = 2;
    v2[8] = 1; v2[9] = 1; v2[10] = 1; v2[11] = 1;
    v2[12] = 1; v2[13] = 0; v2[14] = 0; v2[15] = 1;

    data.bounds.min = Vec3(-1, -1, 0);
    data.bounds.max = Vec3(3, 3, 0);

    return data;
}

} // namespace spt3d
