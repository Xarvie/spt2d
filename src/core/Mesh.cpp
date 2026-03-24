#define _USE_MATH_DEFINES
#include <cmath>

#include "../Spt3D.h"
#include "../glad/glad.h"

#include <vector>

namespace spt3d {

struct Mesh::Impl {
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ibo = 0;
    int indexCount = 0;
    int vertexCount = 0;
    bool indexed = false;
    bool dynamic = false;
    
    Impl() {
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glGenBuffers(1, &ibo);
    }
    
    ~Impl() {
        if (vao) glDeleteVertexArrays(1, &vao);
        if (vbo) glDeleteBuffers(1, &vbo);
        if (ibo) glDeleteBuffers(1, &ibo);
    }
};

bool Mesh::Valid() const { return p && p->vao != 0; }
Mesh::operator bool() const { return Valid(); }

struct MeshBuilder::Impl {
    std::vector<float> positions;
    std::vector<float> normals;
    std::vector<float> tangents;
    std::vector<float> uv0;
    std::vector<float> uv1;
    std::vector<uint8_t> colors;
    std::vector<uint8_t> boneIdx;
    std::vector<float> boneWt;
    std::vector<uint16_t> idx16;
    std::vector<uint32_t> idx32;
    bool dynamic = false;
    bool hasIdx32 = false;
};

MeshBuilder::MeshBuilder() : p(std::make_unique<Impl>()) {}

MeshBuilder& MeshBuilder::Positions(const Vec3* data, int n) {
    p->positions.resize(n * 3);
    for (int i = 0; i < n; ++i) {
        p->positions[i * 3 + 0] = data[i].x;
        p->positions[i * 3 + 1] = data[i].y;
        p->positions[i * 3 + 2] = data[i].z;
    }
    return *this;
}

MeshBuilder& MeshBuilder::Normals(const Vec3* data, int n) {
    p->normals.resize(n * 3);
    for (int i = 0; i < n; ++i) {
        p->normals[i * 3 + 0] = data[i].x;
        p->normals[i * 3 + 1] = data[i].y;
        p->normals[i * 3 + 2] = data[i].z;
    }
    return *this;
}

MeshBuilder& MeshBuilder::Tangents(const Vec4* data, int n) {
    p->tangents.resize(n * 4);
    for (int i = 0; i < n; ++i) {
        p->tangents[i * 4 + 0] = data[i].x;
        p->tangents[i * 4 + 1] = data[i].y;
        p->tangents[i * 4 + 2] = data[i].z;
        p->tangents[i * 4 + 3] = data[i].w;
    }
    return *this;
}

MeshBuilder& MeshBuilder::UV0(const Vec2* data, int n) {
    p->uv0.resize(n * 2);
    for (int i = 0; i < n; ++i) {
        p->uv0[i * 2 + 0] = data[i].x;
        p->uv0[i * 2 + 1] = data[i].y;
    }
    return *this;
}

MeshBuilder& MeshBuilder::UV1(const Vec2* data, int n) {
    p->uv1.resize(n * 2);
    for (int i = 0; i < n; ++i) {
        p->uv1[i * 2 + 0] = data[i].x;
        p->uv1[i * 2 + 1] = data[i].y;
    }
    return *this;
}

MeshBuilder& MeshBuilder::VertColors(const Color* data, int n) {
    p->colors.resize(n * 4);
    for (int i = 0; i < n; ++i) {
        p->colors[i * 4 + 0] = data[i].r;
        p->colors[i * 4 + 1] = data[i].g;
        p->colors[i * 4 + 2] = data[i].b;
        p->colors[i * 4 + 3] = data[i].a;
    }
    return *this;
}

MeshBuilder& MeshBuilder::BoneIdx(const uint8_t* data, int n) {
    p->boneIdx.assign(data, data + n * 4);
    return *this;
}

MeshBuilder& MeshBuilder::BoneWt(const Vec4* data, int n) {
    p->boneWt.resize(n * 4);
    for (int i = 0; i < n; ++i) {
        p->boneWt[i * 4 + 0] = data[i].x;
        p->boneWt[i * 4 + 1] = data[i].y;
        p->boneWt[i * 4 + 2] = data[i].z;
        p->boneWt[i * 4 + 3] = data[i].w;
    }
    return *this;
}

MeshBuilder& MeshBuilder::Idx16(const uint16_t* data, int n) {
    p->idx16.assign(data, data + n);
    p->hasIdx32 = false;
    return *this;
}

MeshBuilder& MeshBuilder::Idx32(const uint32_t* data, int n) {
    p->idx32.assign(data, data + n);
    p->hasIdx32 = true;
    return *this;
}

MeshBuilder& MeshBuilder::Dynamic(bool d) {
    p->dynamic = d;
    return *this;
}

Mesh MeshBuilder::Build() {
    Mesh mesh;
    mesh.p = std::make_shared<Mesh::Impl>();
    
    int vertexCount = static_cast<int>(p->positions.size() / 3);
    if (vertexCount == 0) return mesh;
    
    mesh.p->vertexCount = vertexCount;
    mesh.p->dynamic = p->dynamic;
    
    std::vector<float> interleaved;
    interleaved.reserve(vertexCount * 14);
    
    for (int i = 0; i < vertexCount; ++i) {
        if (i * 3 + 2 < p->positions.size()) {
            interleaved.push_back(p->positions[i * 3 + 0]);
            interleaved.push_back(p->positions[i * 3 + 1]);
            interleaved.push_back(p->positions[i * 3 + 2]);
        } else {
            interleaved.push_back(0.0f);
            interleaved.push_back(0.0f);
            interleaved.push_back(0.0f);
        }
        
        if (i * 3 + 2 < p->normals.size()) {
            interleaved.push_back(p->normals[i * 3 + 0]);
            interleaved.push_back(p->normals[i * 3 + 1]);
            interleaved.push_back(p->normals[i * 3 + 2]);
        } else {
            interleaved.push_back(0.0f);
            interleaved.push_back(1.0f);
            interleaved.push_back(0.0f);
        }
        
        if (i * 2 + 1 < p->uv0.size()) {
            interleaved.push_back(p->uv0[i * 2 + 0]);
            interleaved.push_back(p->uv0[i * 2 + 1]);
        } else {
            interleaved.push_back(0.0f);
            interleaved.push_back(0.0f);
        }
        
        if (i * 4 + 3 < p->colors.size()) {
            interleaved.push_back(p->colors[i * 4 + 0] / 255.0f);
            interleaved.push_back(p->colors[i * 4 + 1] / 255.0f);
            interleaved.push_back(p->colors[i * 4 + 2] / 255.0f);
            interleaved.push_back(p->colors[i * 4 + 3] / 255.0f);
        } else {
            interleaved.push_back(1.0f);
            interleaved.push_back(1.0f);
            interleaved.push_back(1.0f);
            interleaved.push_back(1.0f);
        }
        
        if (i * 4 + 3 < p->tangents.size()) {
            interleaved.push_back(p->tangents[i * 4 + 0]);
            interleaved.push_back(p->tangents[i * 4 + 1]);
            interleaved.push_back(p->tangents[i * 4 + 2]);
            interleaved.push_back(p->tangents[i * 4 + 3]);
        } else {
            interleaved.push_back(1.0f);
            interleaved.push_back(0.0f);
            interleaved.push_back(0.0f);
            interleaved.push_back(1.0f);
        }
    }
    
    glBindVertexArray(mesh.p->vao);
    
    glBindBuffer(GL_ARRAY_BUFFER, mesh.p->vbo);
    glBufferData(GL_ARRAY_BUFFER, interleaved.size() * sizeof(float), interleaved.data(),
                 p->dynamic ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);
    
    GLsizei stride = 14 * sizeof(float);
    
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
    
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
    
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void*)(6 * sizeof(float)));
    
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, (void*)(8 * sizeof(float)));
    
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, stride, (void*)(12 * sizeof(float)));
    
    if (!p->idx16.empty() || !p->idx32.empty()) {
        mesh.p->indexed = true;
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.p->ibo);
        
        if (p->hasIdx32 && !p->idx32.empty()) {
            mesh.p->indexCount = static_cast<int>(p->idx32.size());
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, p->idx32.size() * sizeof(uint32_t), 
                         p->idx32.data(), GL_STATIC_DRAW);
        } else if (!p->idx16.empty()) {
            mesh.p->indexCount = static_cast<int>(p->idx16.size());
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, p->idx16.size() * sizeof(uint16_t), 
                         p->idx16.data(), GL_STATIC_DRAW);
        }
    } else {
        mesh.p->indexed = false;
        mesh.p->indexCount = vertexCount;
    }
    
    glBindVertexArray(0);
    
    return mesh;
}

void UpdatePos(Mesh m, const Vec3* data, int n, int off) {
    if (!m.Valid() || !data || n <= 0) return;
    glBindBuffer(GL_ARRAY_BUFFER, m.p->vbo);
    glBufferSubData(GL_ARRAY_BUFFER, off * 14 * sizeof(float), n * 3 * sizeof(float), data);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void UpdateIdx(Mesh m, const uint16_t* data, int n, int off) {
    if (!m.Valid() || !data || n <= 0) return;
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.p->ibo);
    glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, off * sizeof(uint16_t), n * sizeof(uint16_t), data);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

void UpdateUV(Mesh m, const Vec2* data, int n, int off) {
    if (!m.Valid() || !data || n <= 0) return;
    std::vector<float> uvData(n * 2);
    for (int i = 0; i < n; ++i) {
        uvData[i * 2 + 0] = data[i].x;
        uvData[i * 2 + 1] = data[i].y;
    }
    glBindBuffer(GL_ARRAY_BUFFER, m.p->vbo);
    glBufferSubData(GL_ARRAY_BUFFER, (off * 14 + 6) * sizeof(float), n * 2 * sizeof(float), uvData.data());
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

Mesh GenPlane(float w, float h, int sx, int sz) {
    MeshBuilder builder;
    
    std::vector<Vec3> pos;
    std::vector<Vec3> norm;
    std::vector<Vec2> uv;
    std::vector<uint16_t> idx;
    
    float hw = w * 0.5f;
    float hh = h * 0.5f;
    
    for (int z = 0; z <= sz; ++z) {
        for (int x = 0; x <= sx; ++x) {
            float fx = static_cast<float>(x) / sx;
            float fz = static_cast<float>(z) / sz;
            
            pos.push_back({ (fx - 0.5f) * w, 0.0f, (fz - 0.5f) * h });
            norm.push_back({ 0.0f, 1.0f, 0.0f });
            uv.push_back({ fx, fz });
        }
    }
    
    for (int z = 0; z < sz; ++z) {
        for (int x = 0; x < sx; ++x) {
            int i = z * (sx + 1) + x;
            idx.push_back(static_cast<uint16_t>(i));
            idx.push_back(static_cast<uint16_t>(i + sx + 1));
            idx.push_back(static_cast<uint16_t>(i + 1));
            
            idx.push_back(static_cast<uint16_t>(i + 1));
            idx.push_back(static_cast<uint16_t>(i + sx + 1));
            idx.push_back(static_cast<uint16_t>(i + sx + 2));
        }
    }
    
    return builder.Positions(pos.data(), static_cast<int>(pos.size()))
                 .Normals(norm.data(), static_cast<int>(norm.size()))
                 .UV0(uv.data(), static_cast<int>(uv.size()))
                 .Idx16(idx.data(), static_cast<int>(idx.size()))
                 .Build();
}

Mesh GenCube(float w, float h, float d) {
    MeshBuilder builder;
    
    float hw = w * 0.5f, hh = h * 0.5f, hd = d * 0.5f;
    
    std::vector<Vec3> pos = {
        { -hw, -hh,  hd }, {  hw, -hh,  hd }, {  hw,  hh,  hd }, { -hw,  hh,  hd },
        { -hw, -hh, -hd }, { -hw,  hh, -hd }, {  hw,  hh, -hd }, {  hw, -hh, -hd },
        { -hw,  hh, -hd }, { -hw,  hh,  hd }, {  hw,  hh,  hd }, {  hw,  hh, -hd },
        { -hw, -hh, -hd }, {  hw, -hh, -hd }, {  hw, -hh,  hd }, { -hw, -hh,  hd },
        {  hw, -hh, -hd }, {  hw,  hh, -hd }, {  hw,  hh,  hd }, {  hw, -hh,  hd },
        { -hw, -hh, -hd }, { -hw, -hh,  hd }, { -hw,  hh,  hd }, { -hw,  hh, -hd }
    };
    
    std::vector<Vec3> norm = {
        { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 },
        { 0, 0, -1 }, { 0, 0, -1 }, { 0, 0, -1 }, { 0, 0, -1 },
        { 0, 1, 0 }, { 0, 1, 0 }, { 0, 1, 0 }, { 0, 1, 0 },
        { 0, -1, 0 }, { 0, -1, 0 }, { 0, -1, 0 }, { 0, -1, 0 },
        { 1, 0, 0 }, { 1, 0, 0 }, { 1, 0, 0 }, { 1, 0, 0 },
        { -1, 0, 0 }, { -1, 0, 0 }, { -1, 0, 0 }, { -1, 0, 0 }
    };
    
    std::vector<Vec2> uv = {
        { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 },
        { 1, 0 }, { 1, 1 }, { 0, 1 }, { 0, 0 },
        { 0, 1 }, { 0, 0 }, { 1, 0 }, { 1, 1 },
        { 1, 1 }, { 0, 1 }, { 0, 0 }, { 1, 0 },
        { 1, 0 }, { 1, 1 }, { 0, 1 }, { 0, 0 },
        { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 }
    };
    
    std::vector<uint16_t> idx;
    for (int face = 0; face < 6; ++face) {
        int base = face * 4;
        idx.push_back(static_cast<uint16_t>(base + 0));
        idx.push_back(static_cast<uint16_t>(base + 1));
        idx.push_back(static_cast<uint16_t>(base + 2));
        idx.push_back(static_cast<uint16_t>(base + 0));
        idx.push_back(static_cast<uint16_t>(base + 2));
        idx.push_back(static_cast<uint16_t>(base + 3));
    }
    
    return builder.Positions(pos.data(), static_cast<int>(pos.size()))
                 .Normals(norm.data(), static_cast<int>(norm.size()))
                 .UV0(uv.data(), static_cast<int>(uv.size()))
                 .Idx16(idx.data(), static_cast<int>(idx.size()))
                 .Build();
}

Mesh GenSphere(float r, int rings, int slices) {
    MeshBuilder builder;
    
    std::vector<Vec3> pos;
    std::vector<Vec3> norm;
    std::vector<Vec2> uv;
    std::vector<uint16_t> idx;
    
    for (int ring = 0; ring <= rings; ++ring) {
        float phi = M_PI * static_cast<float>(ring) / rings;
        for (int slice = 0; slice <= slices; ++slice) {
            float theta = 2.0f * M_PI * static_cast<float>(slice) / slices;
            
            float x = std::sin(phi) * std::cos(theta);
            float y = std::cos(phi);
            float z = std::sin(phi) * std::sin(theta);
            
            pos.push_back({ x * r, y * r, z * r });
            norm.push_back({ x, y, z });
            uv.push_back({ static_cast<float>(slice) / slices, static_cast<float>(ring) / rings });
        }
    }
    
    for (int ring = 0; ring < rings; ++ring) {
        for (int slice = 0; slice < slices; ++slice) {
            int i = ring * (slices + 1) + slice;
            
            idx.push_back(static_cast<uint16_t>(i));
            idx.push_back(static_cast<uint16_t>(i + slices + 1));
            idx.push_back(static_cast<uint16_t>(i + 1));
            
            idx.push_back(static_cast<uint16_t>(i + 1));
            idx.push_back(static_cast<uint16_t>(i + slices + 1));
            idx.push_back(static_cast<uint16_t>(i + slices + 2));
        }
    }
    
    return builder.Positions(pos.data(), static_cast<int>(pos.size()))
                 .Normals(norm.data(), static_cast<int>(norm.size()))
                 .UV0(uv.data(), static_cast<int>(uv.size()))
                 .Idx16(idx.data(), static_cast<int>(idx.size()))
                 .Build();
}

Mesh GenHemiSphere(float r, int rings, int slices) {
    MeshBuilder builder;
    
    std::vector<Vec3> pos;
    std::vector<Vec3> norm;
    std::vector<Vec2> uv;
    std::vector<uint16_t> idx;
    
    for (int ring = 0; ring <= rings; ++ring) {
        float phi = M_PI * 0.5f * static_cast<float>(ring) / rings;
        for (int slice = 0; slice <= slices; ++slice) {
            float theta = 2.0f * M_PI * static_cast<float>(slice) / slices;
            
            float x = std::sin(phi) * std::cos(theta);
            float y = std::cos(phi);
            float z = std::sin(phi) * std::sin(theta);
            
            pos.push_back({ x * r, y * r, z * r });
            norm.push_back({ x, y, z });
            uv.push_back({ static_cast<float>(slice) / slices, static_cast<float>(ring) / rings });
        }
    }
    
    for (int ring = 0; ring < rings; ++ring) {
        for (int slice = 0; slice < slices; ++slice) {
            int i = ring * (slices + 1) + slice;
            
            idx.push_back(static_cast<uint16_t>(i));
            idx.push_back(static_cast<uint16_t>(i + slices + 1));
            idx.push_back(static_cast<uint16_t>(i + 1));
            
            idx.push_back(static_cast<uint16_t>(i + 1));
            idx.push_back(static_cast<uint16_t>(i + slices + 1));
            idx.push_back(static_cast<uint16_t>(i + slices + 2));
        }
    }
    
    return builder.Positions(pos.data(), static_cast<int>(pos.size()))
                 .Normals(norm.data(), static_cast<int>(norm.size()))
                 .UV0(uv.data(), static_cast<int>(uv.size()))
                 .Idx16(idx.data(), static_cast<int>(idx.size()))
                 .Build();
}

Mesh GenCylinder(float r, float h, int slices) {
    MeshBuilder builder;
    
    std::vector<Vec3> pos;
    std::vector<Vec3> norm;
    std::vector<Vec2> uv;
    std::vector<uint16_t> idx;
    
    float hh = h * 0.5f;
    
    for (int slice = 0; slice <= slices; ++slice) {
        float theta = 2.0f * M_PI * static_cast<float>(slice) / slices;
        float x = std::cos(theta);
        float z = std::sin(theta);
        
        pos.push_back({ x * r, -hh, z * r });
        norm.push_back({ x, 0.0f, z });
        uv.push_back({ static_cast<float>(slice) / slices, 0.0f });
        
        pos.push_back({ x * r, hh, z * r });
        norm.push_back({ x, 0.0f, z });
        uv.push_back({ static_cast<float>(slice) / slices, 1.0f });
    }
    
    for (int slice = 0; slice < slices; ++slice) {
        int i = slice * 2;
        
        idx.push_back(static_cast<uint16_t>(i));
        idx.push_back(static_cast<uint16_t>(i + 1));
        idx.push_back(static_cast<uint16_t>(i + 2));
        
        idx.push_back(static_cast<uint16_t>(i + 2));
        idx.push_back(static_cast<uint16_t>(i + 1));
        idx.push_back(static_cast<uint16_t>(i + 3));
    }
    
    return builder.Positions(pos.data(), static_cast<int>(pos.size()))
                 .Normals(norm.data(), static_cast<int>(norm.size()))
                 .UV0(uv.data(), static_cast<int>(uv.size()))
                 .Idx16(idx.data(), static_cast<int>(idx.size()))
                 .Build();
}

Mesh GenCone(float r, float h, int slices) {
    MeshBuilder builder;
    
    std::vector<Vec3> pos;
    std::vector<Vec3> norm;
    std::vector<Vec2> uv;
    std::vector<uint16_t> idx;
    
    float hh = h * 0.5f;
    
    pos.push_back({ 0.0f, hh, 0.0f });
    norm.push_back({ 0.0f, 1.0f, 0.0f });
    uv.push_back({ 0.5f, 1.0f });
    
    for (int slice = 0; slice <= slices; ++slice) {
        float theta = 2.0f * M_PI * static_cast<float>(slice) / slices;
        float x = std::cos(theta);
        float z = std::sin(theta);
        
        float ny = r / h;
        float len = std::sqrt(x * x + ny * ny + z * z);
        
        pos.push_back({ x * r, -hh, z * r });
        norm.push_back({ x / len, ny / len, z / len });
        uv.push_back({ static_cast<float>(slice) / slices, 0.0f });
    }
    
    for (int slice = 0; slice < slices; ++slice) {
        idx.push_back(0);
        idx.push_back(static_cast<uint16_t>(slice + 2));
        idx.push_back(static_cast<uint16_t>(slice + 1));
    }
    
    return builder.Positions(pos.data(), static_cast<int>(pos.size()))
                 .Normals(norm.data(), static_cast<int>(norm.size()))
                 .UV0(uv.data(), static_cast<int>(uv.size()))
                 .Idx16(idx.data(), static_cast<int>(idx.size()))
                 .Build();
}

Mesh GenTorus(float r, float tube, int rseg, int tseg) {
    MeshBuilder builder;
    
    std::vector<Vec3> pos;
    std::vector<Vec3> norm;
    std::vector<Vec2> uv;
    std::vector<uint16_t> idx;
    
    for (int i = 0; i <= rseg; ++i) {
        float u = static_cast<float>(i) / rseg * M_PI * 2.0f;
        for (int j = 0; j <= tseg; ++j) {
            float v = static_cast<float>(j) / tseg * M_PI * 2.0f;
            
            float x = (r + tube * std::cos(v)) * std::cos(u);
            float y = tube * std::sin(v);
            float z = (r + tube * std::cos(v)) * std::sin(u);
            
            pos.push_back({ x, y, z });
            
            float nx = std::cos(v) * std::cos(u);
            float ny = std::sin(v);
            float nz = std::cos(v) * std::sin(u);
            norm.push_back({ nx, ny, nz });
            
            uv.push_back({ static_cast<float>(i) / rseg, static_cast<float>(j) / tseg });
        }
    }
    
    for (int i = 0; i < rseg; ++i) {
        for (int j = 0; j < tseg; ++j) {
            int p0 = i * (tseg + 1) + j;
            int p1 = p0 + 1;
            int p2 = (i + 1) * (tseg + 1) + j;
            int p3 = p2 + 1;
            
            idx.push_back(static_cast<uint16_t>(p0));
            idx.push_back(static_cast<uint16_t>(p2));
            idx.push_back(static_cast<uint16_t>(p1));
            
            idx.push_back(static_cast<uint16_t>(p1));
            idx.push_back(static_cast<uint16_t>(p2));
            idx.push_back(static_cast<uint16_t>(p3));
        }
    }
    
    return builder.Positions(pos.data(), static_cast<int>(pos.size()))
                 .Normals(norm.data(), static_cast<int>(norm.size()))
                 .UV0(uv.data(), static_cast<int>(uv.size()))
                 .Idx16(idx.data(), static_cast<int>(idx.size()))
                 .Build();
}

Mesh GenCapsule(float r, float h, int rings, int slices) {
    MeshBuilder builder;
    
    std::vector<Vec3> pos;
    std::vector<Vec3> norm;
    std::vector<Vec2> uv;
    std::vector<uint16_t> idx;
    
    float hh = h * 0.5f - r;
    if (hh < 0) hh = 0;
    
    for (int ring = 0; ring <= rings; ++ring) {
        float phi = M_PI * static_cast<float>(ring) / rings;
        for (int slice = 0; slice <= slices; ++slice) {
            float theta = 2.0f * M_PI * static_cast<float>(slice) / slices;
            
            float x = std::sin(phi) * std::cos(theta);
            float y = std::cos(phi);
            float z = std::sin(phi) * std::sin(theta);
            
            float yOffset = (ring <= rings / 2) ? hh : -hh;
            
            pos.push_back({ x * r, y * r + yOffset, z * r });
            norm.push_back({ x, y, z });
            uv.push_back({ static_cast<float>(slice) / slices, static_cast<float>(ring) / rings });
        }
    }
    
    for (int ring = 0; ring < rings; ++ring) {
        for (int slice = 0; slice < slices; ++slice) {
            int i = ring * (slices + 1) + slice;
            
            idx.push_back(static_cast<uint16_t>(i));
            idx.push_back(static_cast<uint16_t>(i + slices + 1));
            idx.push_back(static_cast<uint16_t>(i + 1));
            
            idx.push_back(static_cast<uint16_t>(i + 1));
            idx.push_back(static_cast<uint16_t>(i + slices + 1));
            idx.push_back(static_cast<uint16_t>(i + slices + 2));
        }
    }
    
    return builder.Positions(pos.data(), static_cast<int>(pos.size()))
                 .Normals(norm.data(), static_cast<int>(norm.size()))
                 .UV0(uv.data(), static_cast<int>(uv.size()))
                 .Idx16(idx.data(), static_cast<int>(idx.size()))
                 .Build();
}

Mesh GenFullscreenTri() {
    MeshBuilder builder;
    
    std::vector<Vec3> pos = {
        { -1.0f, -1.0f, 0.0f },
        {  3.0f, -1.0f, 0.0f },
        { -1.0f,  3.0f, 0.0f }
    };
    
    std::vector<Vec2> uv = {
        { 0.0f, 0.0f },
        { 2.0f, 0.0f },
        { 0.0f, 2.0f }
    };
    
    return builder.Positions(pos.data(), 3)
                 .UV0(uv.data(), 3)
                 .Build();
}

}
