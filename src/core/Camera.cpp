#include "../Spt3D.h"

namespace spt3d {

Mat4 ViewMat(const Camera3D& c) {
    return glm::lookAt(c.position, c.target, c.up);
}

Mat4 ProjMat(const Camera3D& c, float aspect) {
    if (c.ortho) {
        float size = c.fov * 0.5f;
        return glm::ortho(-size * aspect, size * aspect, -size, size, c.near_clip, c.far_clip);
    }
    return glm::perspective(glm::radians(c.fov), aspect, c.near_clip, c.far_clip);
}

Mat4 ViewProjMat(const Camera3D& c, float aspect) {
    return ProjMat(c, aspect) * ViewMat(c);
}

Mat4 CamMat2D(const Camera2D& c) {
    Mat4 mat = Mat4(1.0f);
    mat = glm::translate(mat, Vec3(c.offset.x, c.offset.y, 0.0f));
    mat = glm::rotate(mat, glm::radians(c.rotation), Vec3(0, 0, 1));
    mat = glm::scale(mat, Vec3(c.zoom, c.zoom, 1.0f));
    mat = glm::translate(mat, Vec3(-c.target.x, -c.target.y, 0.0f));
    return mat;
}

Ray ScreenRay(const Camera3D& c, Vec2 screen, int sw, int sh) {
    float aspect = static_cast<float>(sw) / static_cast<float>(sh);
    Mat4 proj = ProjMat(c, aspect);
    Mat4 view = ViewMat(c);
    Mat4 invVP = glm::inverse(proj * view);
    
    float ndcX = (2.0f * screen.x) / sw - 1.0f;
    float ndcY = 1.0f - (2.0f * screen.y) / sh;
    
    Vec4 nearPoint(ndcX, ndcY, -1.0f, 1.0f);
    Vec4 farPoint(ndcX, ndcY, 1.0f, 1.0f);
    
    Vec4 nearWorld = invVP * nearPoint;
    nearWorld /= nearWorld.w;
    
    Vec4 farWorld = invVP * farPoint;
    farWorld /= farWorld.w;
    
    Vec3 direction = glm::normalize(Vec3(farWorld) - Vec3(nearWorld));
    
    Ray ray;
    ray.origin = c.position;
    ray.dir = direction;
    return ray;
}

Vec2 WorldToScreen3D(const Camera3D& c, Vec3 pos, int sw, int sh) {
    float aspect = static_cast<float>(sw) / static_cast<float>(sh);
    Mat4 vp = ViewProjMat(c, aspect);
    Vec4 clip = vp * Vec4(pos, 1.0f);
    
    if (clip.w == 0.0f) return Vec2(0, 0);
    
    Vec3 ndc = Vec3(clip) / clip.w;
    return Vec2(
        (ndc.x + 1.0f) * 0.5f * sw,
        (1.0f - ndc.y) * 0.5f * sh
    );
}

Vec3 ScreenToWorld3D(const Camera3D& c, Vec2 screen, float depth, int sw, int sh) {
    float aspect = static_cast<float>(sw) / static_cast<float>(sh);
    Mat4 invVP = glm::inverse(ViewProjMat(c, aspect));
    
    float ndcX = (2.0f * screen.x) / sw - 1.0f;
    float ndcY = 1.0f - (2.0f * screen.y) / sh;
    
    Vec4 clip(ndcX, ndcY, depth * 2.0f - 1.0f, 1.0f);
    Vec4 world = invVP * clip;
    
    if (world.w == 0.0f) return Vec3(0);
    return Vec3(world) / world.w;
}

Vec2 ScreenToWorld2D(const Camera2D& c, Vec2 screen) {
    Mat4 invMat = glm::inverse(CamMat2D(c));
    Vec4 world = invMat * Vec4(screen.x, screen.y, 0.0f, 1.0f);
    return Vec2(world.x, world.y);
}

Vec2 WorldToScreen2D(const Camera2D& c, Vec2 world) {
    Mat4 mat = CamMat2D(c);
    Vec4 screen = mat * Vec4(world.x, world.y, 0.0f, 1.0f);
    return Vec2(screen.x, screen.y);
}

Mat4 ToMat4(Transform t) {
    Mat4 m = Mat4(1.0f);
    m = glm::translate(m, t.pos);
    m = m * glm::mat4_cast(t.rot);
    m = glm::scale(m, t.scl);
    return m;
}

Transform FromMat4(Mat4 m) {
    Transform t;
    t.pos = Vec3(m[3]);
    t.rot = glm::quat_cast(m);
    
    Vec3 scale;
    scale.x = glm::length(Vec3(m[0]));
    scale.y = glm::length(Vec3(m[1]));
    scale.z = glm::length(Vec3(m[2]));
    t.scl = scale;
    
    return t;
}

Transform TLerp(Transform a, Transform b, float t) {
    Transform result;
    result.pos = glm::mix(a.pos, b.pos, t);
    result.rot = glm::slerp(a.rot, b.rot, t);
    result.scl = glm::mix(a.scl, b.scl, t);
    return result;
}

}
