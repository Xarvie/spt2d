#include "../Spt3D.h"
#include "../glad/glad.h"

#include <vector>
#include <cmath>

namespace spt3d {

static std::vector<Vec3> g_dbgVertices;
static std::vector<Color> g_dbgColors;
static Shader g_dbgShader;
static GLuint g_dbgVAO = 0;
static GLuint g_dbgVBO = 0;
static GLuint g_dbgCBO = 0;

static void ensureDbgResources() {
    if (!g_dbgShader.Ready()) {
        const char* vs = R"(#version 300 es
            layout(location=0) in vec3 a_pos;
            layout(location=3) in vec4 a_color;
            uniform mat4 u_mvp;
            out vec4 v_color;
            void main() {
                gl_Position = u_mvp * vec4(a_pos, 1.0);
                v_color = a_color;
            }
        )";
        const char* fs = R"(#version 300 es
            precision mediump float;
            in vec4 v_color;
            out vec4 fragColor;
            void main() { fragColor = v_color; }
        )";
        g_dbgShader = CreateSimpleShader(vs, fs);
    }
    
    if (g_dbgVAO == 0) {
        glGenVertexArrays(1, &g_dbgVAO);
        glGenBuffers(1, &g_dbgVBO);
        glGenBuffers(1, &g_dbgCBO);
    }
}

void DbgLine(Vec3 a, Vec3 b, Color c) {
    g_dbgVertices.push_back(a);
    g_dbgVertices.push_back(b);
    g_dbgColors.push_back(c);
    g_dbgColors.push_back(c);
}

void DbgCube(Vec3 pos, Vec3 size, Color c) {
    float hx = size.x * 0.5f;
    float hy = size.y * 0.5f;
    float hz = size.z * 0.5f;
    
    Vec3 v[8] = {
        {pos.x - hx, pos.y - hy, pos.z - hz},
        {pos.x + hx, pos.y - hy, pos.z - hz},
        {pos.x + hx, pos.y + hy, pos.z - hz},
        {pos.x - hx, pos.y + hy, pos.z - hz},
        {pos.x - hx, pos.y - hy, pos.z + hz},
        {pos.x + hx, pos.y - hy, pos.z + hz},
        {pos.x + hx, pos.y + hy, pos.z + hz},
        {pos.x - hx, pos.y + hy, pos.z + hz}
    };
    
    int edges[24] = {
        0,1, 1,2, 2,3, 3,0,
        4,5, 5,6, 6,7, 7,4,
        0,4, 1,5, 2,6, 3,7
    };
    
    for (int i = 0; i < 24; i += 2) {
        DbgLine(v[edges[i]], v[edges[i+1]], c);
    }
}

void DbgSphere(Vec3 center, float r, Color c, int segments) {
    for (int i = 0; i < segments; ++i) {
        float a1 = 2.0f * 3.14159265f * i / segments;
        float a2 = 2.0f * 3.14159265f * (i + 1) / segments;
        
        DbgLine(
            Vec3(center.x + r * cosf(a1), center.y, center.z + r * sinf(a1)),
            Vec3(center.x + r * cosf(a2), center.y, center.z + r * sinf(a2)),
            c
        );
        DbgLine(
            Vec3(center.x, center.y + r * cosf(a1), center.z + r * sinf(a1)),
            Vec3(center.x, center.y + r * cosf(a2), center.z + r * sinf(a2)),
            c
        );
        DbgLine(
            Vec3(center.x + r * cosf(a1), center.y + r * sinf(a1), center.z),
            Vec3(center.x + r * cosf(a2), center.y + r * sinf(a2), center.z),
            c
        );
    }
}

void DbgCapsule(Vec3 start, Vec3 end, float r, Color c, int segments) {
    DbgLine(start, end, c);
    
    Vec3 mid = (start + end) * 0.5f;
    DbgSphere(start, r, c, segments / 2);
    DbgSphere(end, r, c, segments / 2);
}

void DbgAABB(AABB box, Color c) {
    Vec3 center = (box.min + box.max) * 0.5f;
    Vec3 size = box.max - box.min;
    DbgCube(center, size, c);
}

void DbgFrustum(Mat4 vp, Color c) {
    Vec3 corners[8];
    Mat4 inv = glm::inverse(vp);
    
    Vec4 ndc[8] = {
        {-1, -1, -1, 1}, {1, -1, -1, 1}, {1, 1, -1, 1}, {-1, 1, -1, 1},
        {-1, -1,  1, 1}, {1, -1,  1, 1}, {1, 1,  1, 1}, {-1, 1,  1, 1}
    };
    
    for (int i = 0; i < 8; ++i) {
        Vec4 world = inv * ndc[i];
        corners[i] = Vec3(world) / world.w;
    }
    
    int edges[24] = {
        0,1, 1,2, 2,3, 3,0,
        4,5, 5,6, 6,7, 7,4,
        0,4, 1,5, 2,6, 3,7
    };
    
    for (int i = 0; i < 24; i += 2) {
        DbgLine(corners[edges[i]], corners[edges[i+1]], c);
    }
}

void DbgGrid(int slices, float spacing) {
    Color c = {128, 128, 128, 255};
    float half = slices * spacing * 0.5f;
    
    for (int i = -slices; i <= slices; ++i) {
        float x = i * spacing;
        DbgLine(Vec3(x, 0, -half), Vec3(x, 0, half), c);
        DbgLine(Vec3(-half, 0, x), Vec3(half, 0, x), c);
    }
}

void DbgAxis(Vec3 pos, float size) {
    DbgLine(pos, pos + Vec3(size, 0, 0), Color{255, 0, 0, 255});
    DbgLine(pos, pos + Vec3(0, size, 0), Color{0, 255, 0, 255});
    DbgLine(pos, pos + Vec3(0, 0, size), Color{0, 0, 255, 255});
}

void DbgRay(Ray ray, float len, Color c) {
    DbgLine(ray.origin, ray.origin + ray.dir * len, c);
}

void DbgClear() {
    g_dbgVertices.clear();
    g_dbgColors.clear();
}

void DbgFlush(const Camera3D& cam) {
    if (g_dbgVertices.empty()) return;
    
    ensureDbgResources();
    
    float aspect = static_cast<float>(ScreenW()) / static_cast<float>(ScreenH());
    Mat4 view = ViewMat(cam);
    Mat4 proj = ProjMat(cam, aspect);
    Mat4 mvp = proj * view;
    
    g_dbgShader.use();
    g_dbgShader.setMat4("u_mvp", glm::value_ptr(mvp));
    
    glBindVertexArray(g_dbgVAO);
    
    glBindBuffer(GL_ARRAY_BUFFER, g_dbgVBO);
    glBufferData(GL_ARRAY_BUFFER, g_dbgVertices.size() * sizeof(Vec3), g_dbgVertices.data(), GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    
    glBindBuffer(GL_ARRAY_BUFFER, g_dbgCBO);
    glBufferData(GL_ARRAY_BUFFER, g_dbgColors.size() * sizeof(Color), g_dbgColors.data(), GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, 0, nullptr);
    
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(g_dbgVertices.size()));
    
    glBindVertexArray(0);
}

}
