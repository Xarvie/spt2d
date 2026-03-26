// spt3d/resource/MeshData.cpp
// Right-handed coordinate system, CCW front faces, OpenGL depth [-1, 1].
// GLM defaults (no GLM_FORCE_LEFT_HANDED, no GLM_FORCE_DEPTH_ZERO_TO_ONE,
// no GLM_FORCE_CTOR_INIT).

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

            Vec3 pos = (i < static_cast<int>(p->positions.size())) ? p->positions[i] : Vec3(0.0f);
            v[0] = pos.x; v[1] = pos.y; v[2] = pos.z;

            Vec3 norm = hasNormal ? p->normals[i] : Vec3(0.0f, 1.0f, 0.0f);
            v[3] = norm.x; v[4] = norm.y; v[5] = norm.z;

            Vec2 uv = hasUV0 ? p->uv0[i] : Vec2(0.0f);
            v[6] = uv.x; v[7] = uv.y;

            Vec4 col = hasColor ? ToVec4(p->colors[i]) : Vec4(1.0f);
            v[8] = col.r; v[9] = col.g; v[10] = col.b; v[11] = col.a;

            Vec4 tan = hasTangent ? p->tangents[i] : Vec4(1.0f, 0.0f, 0.0f, 1.0f);
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

// =========================================================================
//  Tangent basis helper
// =========================================================================

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

// =========================================================================
//  GenPlaneData — XZ plane, normal +Y, CCW from above
// =========================================================================
//
//  Grid layout (looking down from +Y):
//
//    +Z ^
//       |  v2 --- v3
//       |  |      |
//       |  v0 --- v1
//       +----------> +X
//
//  CCW triangles (from +Y): (v0, v1, v2), (v1, v3, v2)

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

                // position
                v[0] = x; v[1] = 0; v[2] = z;
                // normal +Y (right-hand: cross(+X, +Z) = +Y  ✗, cross(+Z, +X) = +Y? No.
                //   Actually in right-hand: +X cross +Z = -Y. We want +Y, so tangent=+X, bitangent=+Z
                //   means normal = tangent x bitangent only with left-hand convention.
                //   For right-hand: normal = normalize(cross(edge01, edge02)) where edge01=+X, edge02=+Z
                //   cross(+X, +Z) = (0*0 - 0*1, 0*0 - 1*0, 1*1 - 0*0) = (0, 0, 1)? No.
                //   cross((1,0,0), (0,0,1)) = (0*1-0*0, 0*0-1*1, 1*0-0*0) = (0, -1, 0).
                //   So for CCW with normal +Y in right-hand, edge order must be +X then -Z, or
                //   equivalently the winding v0->v1 is +X and v0->v2 is +Z but we flip the triangle.
                //   With winding (v0, v1, v2) where v1 = v0 + (+X, 0, 0) and v2 = v0 + (0, 0, +Z):
                //   face normal = cross(v1-v0, v2-v0) = cross(+X, +Z) = -Y. That's downward.
                //   So we need (v0, v2, v1): cross(+Z, +X) = +Y. Let me verify:
                //   cross((0,0,1), (1,0,0)) = (0*0-1*0, 1*1-0*0, 0*0-0*1) = (0, 1, 0). Yes! +Y.
                //   So (v0, v2, v1) is CCW with normal +Y in right-hand system.
                //   Wait — but that's the SAME winding as the original left-handed code!
                //
                //   Let me re-derive. In OpenGL right-hand, front face = CCW when viewed from outside.
                //   Looking at the plane from +Y (above): the face normal should point toward us (+Y).
                //   CCW from above means the vertices go counter-clockwise AS SEEN FROM +Y.
                //   v0=(0,0,0), v1=(1,0,0), v2=(0,0,1):
                //     From +Y looking down: v0 is at origin, v1 is to the right, v2 is "up" on screen (but +Z).
                //     The sequence v0->v1->v2 goes: right, then up-left. That's clockwise from above.
                //     So cross(v1-v0, v2-v0) = cross(+X, +Z) = -Y (into the ground). CW from above = back face.
                //   v0, v2, v1 (i.e., origin, +Z, +X): from above, go up then right = CCW. Normal = +Y. ✓
                //
                //   Original left-hand code: v0, v2, v1 — same pattern. In LEFT-handed system,
                //   CW is front face. From +Y: v0->v2->v1 is CCW from above, which in left-hand is back face!
                //   Hmm, that doesn't seem right either. Let me reconsider.
                //
                //   Actually GLM_FORCE_LEFT_HANDED doesn't change cross product math — it only affects
                //   lookAt and projection. The winding convention is determined by glFrontFace.
                //   With glFrontFace(GL_CCW) (default), CCW = front regardless of coordinate handedness.
                //   The cross product is always right-hand rule.
                //
                //   So: if original code produced correct normals and the app used GL_CCW throughout,
                //   the winding WAS already CCW. The left-hand macros only affected view/projection.
                //   Let me re-examine: original plane winding: v0, v2, v1, v1, v2, v3.
                //   v0 = (ix, iz), v1 = (ix+1, iz), v2 = (ix, iz+1), v3 = (ix+1, iz+1).
                //   Triangle 1: v0, v2, v1. cross(v2-v0, v1-v0) = cross(+Z, +X) = +Y. ✓ normal up.
                //   With GL_CCW: is v0->v2->v1 CCW from +Y? From above: origin->+Z->+X = CCW. ✓
                //   So this is ALREADY correct for right-hand + GL_CCW!
                //
                //   BUT WAIT: in a left-handed coordinate system, +Z goes INTO the screen.
                //   The original code was designed so that looking from default camera down -Z (left-hand),
                //   the plane faces the camera. Since we're switching to right-hand where camera looks
                //   down -Z and +Z comes toward the viewer, the plane's +Y normal still faces up, which
                //   is correct regardless of handedness for a floor plane.
                //
                //   Conclusion: the winding order in the original code is ALREADY CCW right-hand.
                //   The GLM_FORCE_LEFT_HANDED only changed view/projection matrices, not mesh winding.
                //   However, there's a subtlety: in left-hand with +Z into screen, a sphere's front faces
                //   the camera when normals point toward -Z. Switching to right-hand, front faces should
                //   have normals toward +Z (toward camera). The sphere normals point outward radially,
                //   so both work — the winding determines which side is "front."
                //
                //   Actually, I need to reconsider the Z-axis flip more carefully for enclosed shapes.
                //   For the cube: face 0 has normal (0,0,+1). In left-hand +Z is away from camera,
                //   so this face points away. In right-hand +Z is toward camera, so this face now
                //   points toward the camera. The winding must be CCW when viewed from +Z.
                //   Original cube face 0: uses quadIdx {0,1,2,0,2,3} = (v0,v1,v2,v0,v2,3).
                //   But wait, the original quad indices were {0,1,2,0,2,3} which is standard CCW quad.
                //   Let me check: p[0]=(x0,y1,z1), p[1]=(x1,y1,z1), p[2]=(x1,y0,z1), p[3]=(x0,y0,z1).
                //   From +Z looking at face: p0=top-left, p1=top-right, p2=bottom-right, p3=bottom-left.
                //   Sequence p0->p1->p2: top-left -> top-right -> bottom-right = CW from +Z! That's wrong
                //   for right-hand CCW.
                //
                //   So the CUBE needs winding reversal, and looking more carefully, the quad pattern
                //   in the original was designed for left-handed convention after all.
                //   Let me re-check the plane: v0,v2,v1 with cross(v2-v0, v1-v0) = +Y is correct
                //   for both conventions since the plane is horizontal.
                //
                //   For the sphere and other shapes, the pattern v0,v2,v1 / v1,v2,v3:
                //   In the sphere, looking at a patch from outside (along -normal direction):
                //   v0=(ring,slice), v1=(ring,slice+1), v2=(ring+1,slice), v3=(ring+1,slice+1).
                //   ring increases from top (+Y) to bottom (-Y).
                //   slice increases with theta (going CCW around Y from +X toward +Z in right-hand).
                //   From outside, v0 is top-left, v1 is top-right, v2 is bottom-left, v3 is bottom-right.
                //   Triangle v0,v2,v1: top-left -> bottom-left -> top-right.
                //   From outside looking in, that's: going down then right = CCW? Let me think...
                //   From outside: v0 top-left, v2 below v0, v1 right of v0.
                //   v0 -> v2 -> v1: down, then up-right. That's counter-clockwise. ✓ for GL_CCW = front.
                //
                //   OK but now consider: in left-hand system, the camera looks down +Z.
                //   A sphere at origin: the part of the sphere facing the camera has normals pointing +Z.
                //   The vertices on that part, when projected onto screen, need to be CCW.
                //   In right-hand system, camera looks down -Z, so the part facing camera has normals -Z.
                //   The same vertices projected... the X axis is the same, Y is the same, but the
                //   "which triangles face the camera" changes because different parts of the sphere
                //   face the camera.
                //
                //   Actually, the winding of each triangle is intrinsic to the mesh and doesn't
                //   depend on the camera. The front face (CCW) always has its computed normal pointing
                //   outward (via right-hand rule for cross product). Since sphere normals point outward,
                //   the CCW winding should make the outward-facing side the front face regardless of
                //   which direction the camera looks.
                //
                //   So the question is: does cross(v2-v0, v1-v0) point outward for the sphere?
                //   At the "equator" front (theta=0): v0 position ≈ (r, 0, 0), normal ≈ (1,0,0).
                //   v1 is slice+1 (theta increases), v2 is ring+1 (phi increases = going down).
                //   edge to v2 goes downward (toward -Y), edge to v1 goes in +theta direction.
                //   +theta from (r,0,0) goes toward (0,0,r) in right-hand (or (0,0,-r) in left-hand).
                //   In right-hand: v1-v0 ≈ (0,0,+dz), v2-v0 ≈ (0,-dy,0).
                //   Original winding: cross(v2-v0, v1-v0) = cross((0,-1,0), (0,0,1)) = (-1,0,0).
                //   That points INWARD (toward -X at this point). That means the front face (CCW)
                //   points inward — i.e., the sphere is inside-out in right-hand!
                //
                //   In left-hand, theta increases toward -Z: v1-v0 ≈ (0,0,-dz).
                //   cross((0,-1,0), (0,0,-1)) = ((-1)*(-1)-0*0, 0*0-0*(-1), 0*0-(-1)*0) = (1,0,0).
                //   Points outward. ✓ So the original winding was correct for LEFT-hand.
                //
                //   Therefore: for RIGHT-hand, I need to REVERSE the winding of all triangles!
                //
                //   For the plane (horizontal, normal +Y): let me re-verify.
                //   Original: v0, v2, v1. v0=(x,-,z), v2=(x,-,z+dz), v1=(x+dx,-,z).
                //   cross(v2-v0, v1-v0) = cross((0,0,dz),(dx,0,0)) = (0*0-dz*0, dz*0-0*0, 0*0-0*dx)
                //   Hmm that gives (0,0,0). Let me be more careful:
                //   cross((0,0,1),(1,0,0)) = (0*0-1*0, 1*1-0*0, 0*0-0*1) = (0,1,0) = +Y. ✓
                //   So the plane IS correct with original winding even in right-hand.
                //
                //   The difference is: for the plane, the winding works in both systems because the
                //   normal is along Y (not Z). For shapes where the relevant faces have Z-component
                //   normals (sphere, cube +Z face, etc.), the theta direction flips with handedness.
                //
                //   Since theta = atan2(z, x) and sin(theta) gives the Z coordinate of a point:
                //   - Left-hand: +Z is away, sin(theta) * r gives z, increasing theta goes clockwise
                //     when viewed from +Y.
                //   - Right-hand: +Z is toward viewer, same formula, but increasing theta goes CCW
                //     from +Y.
                //
                //   The vertex POSITIONS are the same (cos(theta), y, sin(theta)).
                //   But the winding that makes normals point outward differs.
                //
                //   FINAL ANSWER: I need to reverse all triangle winding orders.
                //   Original: (v0, v2, v1), (v1, v2, v3) → New: (v0, v1, v2), (v1, v3, v2)
                //   This will make cross(v1-v0, v2-v0) the face normal, which for the sphere
                //   in right-hand will point outward.
                //
                //   But wait — this will break the plane! With (v0, v1, v2):
                //   cross(v1-v0, v2-v0) = cross((1,0,0),(0,0,1)) = (0,-1,0) = -Y. Wrong!
                //   So the plane needs to KEEP the original winding!
                //
                //   The issue is that the plane doesn't involve the Z-axis direction ambiguity.
                //   Actually, looking at it differently: the theta direction for the sphere goes
                //   x=cos, z=sin. In right-hand, going from theta=0 to theta=pi/2 goes from +X
                //   to +Z, which is CCW from above. The ring direction goes from top to bottom.
                //   From outside looking at the front of the sphere (from +Z toward origin):
                //   slice increases to the LEFT (since from +Z, +X is to the right and increasing
                //   theta goes CCW = to the left from this viewpoint).
                //   ring increases downward.
                //   v0(top-right from outside), v1(top-left), v2(bottom-right), v3(bottom-left).
                //   For CCW from outside: v0->v2->v1 (right, down, then left) or v0->v2->v3->v1.
                //   v0,v2,v1 = original winding! So actually it IS correct?!
                //
                //   I keep going back and forth. Let me just compute numerically.
                //   Sphere, r=1, ring=8 (equator, phi=PI/2), slice=0 (theta=0):
                //   v0: phi=PI*8/16=PI/2, theta=0 → (sin(PI/2)*cos(0), cos(PI/2), sin(PI/2)*sin(0)) = (1, 0, 0)
                //   v1: same ring, slice=1, theta=2PI/16=PI/8 → (sin(PI/2)*cos(PI/8), 0, sin(PI/2)*sin(PI/8))
                //       = (cos(PI/8), 0, sin(PI/8)) ≈ (0.924, 0, 0.383)
                //   v2: ring=9, slice=0, phi=PI*9/16 → (sin(9PI/16)*cos(0), cos(9PI/16), 0)
                //       = (sin(9PI/16), cos(9PI/16), 0) ≈ (0.981, -0.195, 0)
                //   Normal at v0 = (1, 0, 0) pointing outward (+X direction).
                //
                //   Original triangle: v0, v2, v1.
                //   e1 = v2 - v0 = (-0.019, -0.195, 0)
                //   e2 = v1 - v0 = (-0.076, 0, 0.383)
                //   cross(e1, e2) = ((-0.195)(0.383) - 0*(0), 0*(-0.076) - (-0.019)(0.383), (-0.019)(0) - (-0.195)(-0.076))
                //   = (-0.0747, -0.00728, -0.01482)
                //   This points roughly in (-1, 0, 0) direction = INWARD. Bad for right-hand CCW!
                //
                //   Reversed triangle: v0, v1, v2.
                //   e1 = v1 - v0 = (-0.076, 0, 0.383)
                //   e2 = v2 - v0 = (-0.019, -0.195, 0)
                //   cross(e1, e2) = (0*0 - 0.383*(-0.195), 0.383*(-0.019) - (-0.076)*0, (-0.076)*(-0.195) - 0*(-0.019))
                //   = (0.0747, -0.00728, 0.01482)
                //   Points roughly in (+1, 0, 0) = OUTWARD. ✓
                //
                //   Now for the plane: v0=(0,0,0), v1=(1,0,0), v2=(0,0,1).
                //   Reversed: v0, v1, v2.
                //   cross(v1-v0, v2-v0) = cross((1,0,0),(0,0,1)) = (0*1-0*0, 0*0-1*1, 1*0-0*0) = (0,-1,0).
                //   That's -Y = downward. Wrong for a floor plane!
                //
                //   So: sphere needs reversed winding, plane does NOT. Each shape must be handled
                //   individually based on its geometry and normal expectations.
                //
                //   For the plane, I'll keep original winding. For all parametric shapes that use
                //   theta = 2*PI*slice/slices with x=cos(theta), z=sin(theta), the winding needs
                //   to be reversed.
                //
                //   Alternatively, I can flip the theta direction: z = -sin(theta). Then the
                //   original winding would produce outward normals in right-hand. But that changes
                //   vertex positions. Better to just reverse winding per-shape.
                //
                //   CORRECTION: Actually, the cleanest fix is to negate the Z component in the
                //   parametric formula (use z = -sin(theta)) so that the sweep direction matches
                //   right-hand convention, then keep the same winding. But that's more invasive
                //   and changes UV mapping orientation.
                //
                //   SIMPLEST correct approach: reverse winding for parametric shapes (sphere,
                //   hemisphere, cylinder, cone, torus, capsule). Keep plane winding. Fix cube
                //   face vertex order.

                // normal +Y
                v[3] = 0; v[4] = 1; v[5] = 0;
                // uv
                v[6] = static_cast<float>(ix) / sx;
                v[7] = static_cast<float>(iz) / sz;
                // color white
                v[8] = 1; v[9] = 1; v[10] = 1; v[11] = 1;
                // tangent +X
                v[12] = 1; v[13] = 0; v[14] = 0; v[15] = 1;
            }
        }

        // Winding: (v0, v2, v1), (v1, v2, v3) — same as original.
        // This is CCW from +Y in right-hand system (verified above).
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

// =========================================================================
//  GenCubeData — 6 faces, each 4 verts, CCW from outside in right-hand
// =========================================================================

    MeshData GenCubeData(float w, float h, float d) {
        MeshData data;
        const int vertexCount = 24;
        const int indexCount = 36;

        data.vertexCount = vertexCount;
        data.vertices.resize(static_cast<size_t>(vertexCount) * MeshData::kFloatsPerVertex);

        const float hw = w * 0.5f, hh = h * 0.5f, hd = d * 0.5f;

        struct FaceData {
            Vec3 normal; Vec3 tangent;
        };
        FaceData faces[6] = {
                // +Z face: normal (0,0,+1), tangent +X
                {{ 0,  0,  1}, { 1, 0, 0}},
                // -Z face: normal (0,0,-1), tangent -X
                {{ 0,  0, -1}, {-1, 0, 0}},
                // +Y face: normal (0,+1,0), tangent +X
                {{ 0,  1,  0}, { 1, 0, 0}},
                // -Y face: normal (0,-1,0), tangent +X
                {{ 0, -1,  0}, { 1, 0, 0}},
                // +X face: normal (+1,0,0), tangent (0,0,-1)
                {{ 1,  0,  0}, { 0, 0,-1}},
                // -X face: normal (-1,0,0), tangent (0,0,+1)
                {{-1,  0,  0}, { 0, 0, 1}},
        };

        float x0 = -hw, x1 = hw, y0 = -hh, y1 = hh, z0 = -hd, z1 = hd;

        // For each face, p[0]..p[3] are ordered CCW when viewed from outside.
        // UV mapping: p0=(0,0), p1=(1,0), p2=(1,1), p3=(0,1).
        Vec3 faceVerts[6][4];
        Vec2 faceUVs[4] = {{0,0},{1,0},{1,1},{0,1}};

        // +Z face: viewed from +Z, +X is right, +Y is up.
        // CCW from outside: bottom-left, bottom-right, top-right, top-left.
        faceVerts[0][0] = {x0, y0, z1};
        faceVerts[0][1] = {x1, y0, z1};
        faceVerts[0][2] = {x1, y1, z1};
        faceVerts[0][3] = {x0, y1, z1};

        // -Z face: viewed from -Z (camera direction), +X is right, +Y is up.
        // CCW from outside (from -Z direction): bottom-left, bottom-right, top-right, top-left.
        faceVerts[1][0] = {x1, y0, z0};  // ( 0.5, -0.5, -0.5)
        faceVerts[1][1] = {x0, y0, z0};  // (-0.5, -0.5, -0.5)
        faceVerts[1][2] = {x0, y1, z0};  // (-0.5,  0.5, -0.5)
        faceVerts[1][3] = {x1, y1, z0};  // ( 0.5,  0.5, -0.5)

        // +Y face: viewed from +Y, +X is right, -Z is up (OpenGL convention).
        // CCW from outside (from above): bottom-left, bottom-right, top-right, top-left.
        faceVerts[2][0] = {x0, y1, z1};
        faceVerts[2][1] = {x1, y1, z1};
        faceVerts[2][2] = {x1, y1, z0};
        faceVerts[2][3] = {x0, y1, z0};

        // -Y face: viewed from -Y, +X is right, +Z is up.
        // CCW from outside (from below):
        faceVerts[3][0] = {x0, y0, z0};
        faceVerts[3][1] = {x1, y0, z0};
        faceVerts[3][2] = {x1, y0, z1};
        faceVerts[3][3] = {x0, y0, z1};

        // +X face: viewed from +X, -Z is right, +Y is up.
        // CCW from outside:
        faceVerts[4][0] = {x1, y0, z1};
        faceVerts[4][1] = {x1, y0, z0};
        faceVerts[4][2] = {x1, y1, z0};
        faceVerts[4][3] = {x1, y1, z1};

        // -X face: viewed from -X, +Z is right, +Y is up.
        // CCW from outside:
        faceVerts[5][0] = {x0, y0, z0};
        faceVerts[5][1] = {x0, y0, z1};
        faceVerts[5][2] = {x0, y1, z1};
        faceVerts[5][3] = {x0, y1, z0};

        int v = 0;
        for (int f = 0; f < 6; ++f) {
            float* dst = data.vertices.data() + v * MeshData::kFloatsPerVertex;
            for (int i = 0; i < 4; ++i) {
                float* vert = dst + i * MeshData::kFloatsPerVertex;
                vert[0] = faceVerts[f][i].x;
                vert[1] = faceVerts[f][i].y;
                vert[2] = faceVerts[f][i].z;
                vert[3] = faces[f].normal.x;
                vert[4] = faces[f].normal.y;
                vert[5] = faces[f].normal.z;
                vert[6] = faceUVs[i].x;
                vert[7] = faceUVs[i].y;
                vert[8] = 1; vert[9] = 1; vert[10] = 1; vert[11] = 1;
                vert[12] = faces[f].tangent.x;
                vert[13] = faces[f].tangent.y;
                vert[14] = faces[f].tangent.z;
                vert[15] = 1;
            }
            v += 4;
        }

        // Quad indices: CCW (0,1,2), (0,2,3) — CCW from outside.
        data.indices16.resize(indexCount);
        data.indexCount = indexCount;
        uint16_t quadIdx[] = {0, 1, 2, 0, 2, 3};
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

// =========================================================================
//  GenSphereData — parametric sphere, right-hand, CCW outward-facing
// =========================================================================
//
//  Parametric: x = sin(phi)*cos(theta), y = cos(phi), z = sin(phi)*sin(theta)
//  phi in [0, PI] (top to bottom), theta in [0, 2*PI].
//
//  In left-hand code, winding (v0, v2, v1) produced outward normals.
//  In right-hand, we reverse to (v0, v1, v2) for outward normals.

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

                // Tangent: derivative of position w.r.t. theta direction.
                Vec3 tangent = glm::normalize(Vec3(-z, 0.0f, x));
                vert[12] = tangent.x; vert[13] = tangent.y; vert[14] = tangent.z; vert[15] = 1;
                ++v;
            }
        }

        // Reversed winding: (v0, v1, v2), (v1, v3, v2)
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
                data.indices16[idx++] = static_cast<uint16_t>(v1);
                data.indices16[idx++] = static_cast<uint16_t>(v2);
                data.indices16[idx++] = static_cast<uint16_t>(v1);
                data.indices16[idx++] = static_cast<uint16_t>(v3);
                data.indices16[idx++] = static_cast<uint16_t>(v2);
            }
        }

        data.bounds.min = Vec3(-r, -r, -r);
        data.bounds.max = Vec3(r, r, r);

        return data;
    }

// =========================================================================
//  GenHemiSphereData — upper hemisphere, right-hand, CCW
// =========================================================================

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

                Vec3 tangent = glm::normalize(Vec3(-z, 0.0f, x));
                vert[12] = tangent.x; vert[13] = tangent.y; vert[14] = tangent.z; vert[15] = 1;
                ++v;
            }
        }

        // Reversed winding for right-hand
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
                data.indices16[idx++] = static_cast<uint16_t>(v1);
                data.indices16[idx++] = static_cast<uint16_t>(v2);
                data.indices16[idx++] = static_cast<uint16_t>(v1);
                data.indices16[idx++] = static_cast<uint16_t>(v3);
                data.indices16[idx++] = static_cast<uint16_t>(v2);
            }
        }

        data.bounds.min = Vec3(-r, 0, -r);
        data.bounds.max = Vec3(r, r, r);

        return data;
    }

// =========================================================================
//  GenCylinderData — right-hand, CCW
// =========================================================================

    MeshData GenCylinderData(float r, float h, int slices) {
        MeshData data;
        const int rings = 1;
        const int vertexCount = (rings + 3) * (slices + 1);
        const int indexCount = rings * slices * 6 + slices * 6;

        data.vertexCount = vertexCount;
        data.vertices.resize(static_cast<size_t>(vertexCount) * MeshData::kFloatsPerVertex);

        const float halfH = h * 0.5f;
        int v = 0;

        // Side bottom ring
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

        // Side top ring
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

        // Bottom cap ring (normal -Y)
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

        // Top cap ring (normal +Y)
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

        // Side quads — reversed winding for right-hand
        for (int slice = 0; slice < slices; ++slice) {
            int v0 = slice;
            int v1 = slice + 1;
            int v2 = slice + slices + 1;
            int v3 = v2 + 1;

            data.indices16[idx++] = static_cast<uint16_t>(v0);
            data.indices16[idx++] = static_cast<uint16_t>(v1);
            data.indices16[idx++] = static_cast<uint16_t>(v2);
            data.indices16[idx++] = static_cast<uint16_t>(v1);
            data.indices16[idx++] = static_cast<uint16_t>(v3);
            data.indices16[idx++] = static_cast<uint16_t>(v2);
        }

        // Cap fans — reversed winding for right-hand
        // Bottom cap: normal -Y, CCW from below means CW from above.
        // Original: center, slice+1, slice → reversed: center, slice, slice+1.
        // Top cap: normal +Y, CCW from above.
        // Original: center, slice, slice+1 → reversed: center, slice+1, slice.
        int bottomBase = 2 * (slices + 1);
        int topBase = 3 * (slices + 1);
        for (int slice = 0; slice < slices; ++slice) {
            // Bottom cap: fan from bottomBase (slice=0 acts as "center" — but original
            // code used the first vertex as center, which isn't really center of the cap.
            // This is a fan approximation. Reversing the winding:
            data.indices16[idx++] = static_cast<uint16_t>(bottomBase);
            data.indices16[idx++] = static_cast<uint16_t>(bottomBase + slice);
            data.indices16[idx++] = static_cast<uint16_t>(bottomBase + slice + 1);

            // Top cap
            data.indices16[idx++] = static_cast<uint16_t>(topBase);
            data.indices16[idx++] = static_cast<uint16_t>(topBase + slice + 1);
            data.indices16[idx++] = static_cast<uint16_t>(topBase + slice);
        }

        data.bounds.min = Vec3(-r, -halfH, -r);
        data.bounds.max = Vec3(r, halfH, r);

        return data;
    }

// =========================================================================
//  GenConeData — right-hand, CCW
// =========================================================================

    MeshData GenConeData(float r, float h, int slices) {
        MeshData data;
        const int vertexCount = 2 * (slices + 1) + 1;
        const int indexCount = slices * 3 + slices * 3;

        data.vertexCount = vertexCount;
        data.vertices.resize(static_cast<size_t>(vertexCount) * MeshData::kFloatsPerVertex);

        const float halfH = h * 0.5f;
        // Cone side normal: the slope angle gives ny = r/sqrt(r²+h²), but original
        // code used (x, 0, z) for simplicity — we keep the same approximation.
        int v = 0;

        // Base ring (side normals)
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

        // Apex ring (one vertex per slice for smooth normals)
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

        // Bottom center vertex
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

        // Side triangles — reversed winding for right-hand
        // Original: slice, topBase+slice, slice+1 → reversed: slice, slice+1, topBase+slice
        int topBase = slices + 1;
        for (int slice = 0; slice < slices; ++slice) {
            data.indices16[idx++] = static_cast<uint16_t>(slice);
            data.indices16[idx++] = static_cast<uint16_t>(slice + 1);
            data.indices16[idx++] = static_cast<uint16_t>(topBase + slice);
        }

        // Bottom cap — reversed winding
        // Original: center, slice+1, slice → reversed: center, slice, slice+1
        // Wait — bottom cap normal is -Y. For CCW from -Y (below):
        // Looking from -Y upward at the bottom, CCW means the fan goes in the
        // direction where cross product gives -Y. With center at origin projected:
        // center, slice, slice+1 where slice goes CCW from +X toward +Z (from above).
        // From below, that's CW, so the cross product points -Y. Let me verify:
        // center=(0,-halfH,0), s0=(r,-halfH,0), s1=(r*cos(dtheta),-halfH,r*sin(dtheta)).
        // edge1 = s0-center = (r,0,0), edge2 = s1-center ≈ (r,0,r*dtheta).
        // cross(edge1, edge2) = (0*r*dtheta - 0*0, 0*r - r*r*dtheta, r*0 - 0*r) = (0, -r²dtheta, 0).
        // That's -Y direction. ✓ So (center, slice, slice+1) is correct for bottom cap.
        int bottomCenter = 2 * (slices + 1);
        for (int slice = 0; slice < slices; ++slice) {
            data.indices16[idx++] = static_cast<uint16_t>(bottomCenter);
            data.indices16[idx++] = static_cast<uint16_t>(slice);
            data.indices16[idx++] = static_cast<uint16_t>(slice + 1);
        }

        data.bounds.min = Vec3(-r, -halfH, -r);
        data.bounds.max = Vec3(r, halfH, r);

        return data;
    }

// =========================================================================
//  GenTorusData — right-hand, CCW
// =========================================================================

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

                Vec3 tangent = glm::normalize(Vec3(-sinU, 0.0f, cosU));
                vert[12] = tangent.x; vert[13] = tangent.y; vert[14] = tangent.z; vert[15] = 1;
                ++v;
            }
        }

        // Reversed winding for right-hand
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
                data.indices16[idx++] = static_cast<uint16_t>(v1);
                data.indices16[idx++] = static_cast<uint16_t>(v2);
                data.indices16[idx++] = static_cast<uint16_t>(v1);
                data.indices16[idx++] = static_cast<uint16_t>(v3);
                data.indices16[idx++] = static_cast<uint16_t>(v2);
            }
        }

        data.bounds.min = Vec3(-r - tube, -tube, -r - tube);
        data.bounds.max = Vec3(r + tube, tube, r + tube);

        return data;
    }

// =========================================================================
//  GenCapsuleData — right-hand, CCW
// =========================================================================

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

        // Top hemisphere
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

                Vec3 tangent = glm::normalize(Vec3(-z, 0.0f, x));
                vert[12] = tangent.x; vert[13] = tangent.y; vert[14] = tangent.z; vert[15] = 1;
                ++v;
            }
        }

        // Cylinder bottom ring
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

        // Cylinder top ring
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

        // Bottom hemisphere
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

                Vec3 tangent = glm::normalize(Vec3(-z, 0.0f, x));
                vert[12] = tangent.x; vert[13] = tangent.y; vert[14] = tangent.z; vert[15] = 1;
                ++v;
            }
        }

        data.indices16.resize(indexCount);
        data.indexCount = indexCount;
        int idx = 0;

        // Top hemisphere — reversed winding
        for (int ring = 0; ring < hemiRings; ++ring) {
            for (int slice = 0; slice < slices; ++slice) {
                int v0 = ring * (slices + 1) + slice;
                int v1 = v0 + 1;
                int v2 = v0 + slices + 1;
                int v3 = v2 + 1;

                data.indices16[idx++] = static_cast<uint16_t>(v0);
                data.indices16[idx++] = static_cast<uint16_t>(v1);
                data.indices16[idx++] = static_cast<uint16_t>(v2);
                data.indices16[idx++] = static_cast<uint16_t>(v1);
                data.indices16[idx++] = static_cast<uint16_t>(v3);
                data.indices16[idx++] = static_cast<uint16_t>(v2);
            }
        }

        // Cylinder section — reversed winding
        int cylBottomBase = topVertexCount;
        int cylTopBase = topVertexCount + slices + 1;
        for (int slice = 0; slice < slices; ++slice) {
            int v0 = cylBottomBase + slice;
            int v1 = v0 + 1;
            int v2 = cylTopBase + slice;
            int v3 = v2 + 1;

            data.indices16[idx++] = static_cast<uint16_t>(v0);
            data.indices16[idx++] = static_cast<uint16_t>(v1);
            data.indices16[idx++] = static_cast<uint16_t>(v2);
            data.indices16[idx++] = static_cast<uint16_t>(v1);
            data.indices16[idx++] = static_cast<uint16_t>(v3);
            data.indices16[idx++] = static_cast<uint16_t>(v2);
        }

        // Bottom hemisphere — reversed winding
        int bottomBase = topVertexCount + cylVertexCount;
        for (int ring = 0; ring < hemiRings; ++ring) {
            for (int slice = 0; slice < slices; ++slice) {
                int v0 = bottomBase + ring * (slices + 1) + slice;
                int v1 = v0 + 1;
                int v2 = v0 + slices + 1;
                int v3 = v2 + 1;

                data.indices16[idx++] = static_cast<uint16_t>(v0);
                data.indices16[idx++] = static_cast<uint16_t>(v1);
                data.indices16[idx++] = static_cast<uint16_t>(v2);
                data.indices16[idx++] = static_cast<uint16_t>(v1);
                data.indices16[idx++] = static_cast<uint16_t>(v3);
                data.indices16[idx++] = static_cast<uint16_t>(v2);
            }
        }

        const float totalH = cylinderH + 2 * r;
        data.bounds.min = Vec3(-r, -totalH * 0.5f, -r);
        data.bounds.max = Vec3(r, totalH * 0.5f, r);

        return data;
    }

// =========================================================================
//  GenFullscreenTriData — NDC triangle facing +Z (toward camera in right-hand)
// =========================================================================
//
//  Fullscreen triangle covers [-1,1] in NDC. Normal faces +Z (toward camera
//  in right-hand OpenGL, where camera looks down -Z).
//  Winding is CCW when viewed from +Z.

    MeshData GenFullscreenTriData() {
        MeshData data;
        data.vertexCount = 3;
        data.vertices.resize(3 * MeshData::kFloatsPerVertex);

        float* v0 = data.vertices.data();
        float* v1 = v0 + MeshData::kFloatsPerVertex;
        float* v2 = v1 + MeshData::kFloatsPerVertex;

        // CCW from +Z: bottom-left → bottom-right → top-left
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

        // v0(-1,-1) → v1(3,-1) → v2(-1,3): from +Z this goes right then up-left = CCW ✓

        data.bounds.min = Vec3(-1, -1, 0);
        data.bounds.max = Vec3(3, 3, 0);

        return data;
    }

    MeshData GenTriangleData(float size) {
        MeshData data;
        data.vertexCount = 3;
        data.vertices.resize(3 * MeshData::kFloatsPerVertex);

        float* v0 = data.vertices.data();
        float* v1 = v0 + MeshData::kFloatsPerVertex;
        float* v2 = v1 + MeshData::kFloatsPerVertex;

        float s = size * 0.5f;

        v0[0] = 0; v0[1] = s; v0[2] = 0;
        v0[3] = 0; v0[4] = 0; v0[5] = 1;
        v0[6] = 0.5f; v0[7] = 1.0f;
        v0[8] = 1; v0[9] = 0; v0[10] = 0; v0[11] = 1;
        v0[12] = 1; v0[13] = 0; v0[14] = 0; v0[15] = 1;

        v1[0] = s; v1[1] = -s; v1[2] = 0;
        v1[3] = 0; v1[4] = 0; v1[5] = 1;
        v1[6] = 1; v1[7] = 0;
        v1[8] = 0; v1[9] = 0; v1[10] = 1; v1[11] = 1;
        v1[12] = 1; v1[13] = 0; v1[14] = 0; v1[15] = 1;

        v2[0] = -s; v2[1] = -s; v2[2] = 0;
        v2[3] = 0; v2[4] = 0; v2[5] = 1;
        v2[6] = 0; v2[7] = 0;
        v2[8] = 0; v2[9] = 1; v2[10] = 0; v2[11] = 1;
        v2[12] = 1; v2[13] = 0; v2[14] = 0; v2[15] = 1;

        data.bounds.min = Vec3(-s, -s, 0);
        data.bounds.max = Vec3(s, s, 0);

        return data;
    }

} // namespace spt3d