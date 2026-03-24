#include "../Spt3D.h"
#include <algorithm>
#include <cmath>

namespace spt3d {

bool Contains(Rect r, Vec2 p) {
    return p.x >= r.x && p.x <= r.x + r.w &&
           p.y >= r.y && p.y <= r.y + r.h;
}

bool Contains(AABB b, Vec3 p) {
    return p.x >= b.min.x && p.x <= b.max.x &&
           p.y >= b.min.y && p.y <= b.max.y &&
           p.z >= b.min.z && p.z <= b.max.z;
}

bool Overlaps(Rect a, Rect b) {
    return a.x < b.x + b.w && a.x + a.w > b.x &&
           a.y < b.y + b.h && a.y + a.h > b.y;
}

bool Overlaps(AABB a, AABB b) {
    return a.min.x <= b.max.x && a.max.x >= b.min.x &&
           a.min.y <= b.max.y && a.max.y >= b.min.y &&
           a.min.z <= b.max.z && a.max.z >= b.min.z;
}

Rect RectIntersect(Rect a, Rect b) {
    float x1 = std::max(a.x, b.x);
    float y1 = std::max(a.y, b.y);
    float x2 = std::min(a.x + a.w, b.x + b.w);
    float y2 = std::min(a.y + a.h, b.y + b.h);
    if (x1 < x2 && y1 < y2) {
        return { x1, y1, x2 - x1, y2 - y1 };
    }
    return { 0, 0, 0, 0 };
}

AABB AABBMerge(AABB a, AABB b) {
    return {
        Vec3(std::min(a.min.x, b.min.x),
             std::min(a.min.y, b.min.y),
             std::min(a.min.z, b.min.z)),
        Vec3(std::max(a.max.x, b.max.x),
             std::max(a.max.y, b.max.y),
             std::max(a.max.z, b.max.z))
    };
}

AABB AABBTransform(AABB b, Mat4 m) {
    Vec3 corners[8] = {
        {b.min.x, b.min.y, b.min.z},
        {b.max.x, b.min.y, b.min.z},
        {b.min.x, b.max.y, b.min.z},
        {b.max.x, b.max.y, b.min.z},
        {b.min.x, b.min.y, b.max.z},
        {b.max.x, b.min.y, b.max.z},
        {b.min.x, b.max.y, b.max.z},
        {b.max.x, b.max.y, b.max.z}
    };
    
    AABB result = { Vec3(FLT_MAX), Vec3(-FLT_MAX) };
    for (int i = 0; i < 8; ++i) {
        Vec3 t = M4Point(m, corners[i]);
        result.min = glm::min(result.min, t);
        result.max = glm::max(result.max, t);
    }
    return result;
}

RayHit RayVsAABB(Ray ray, AABB box) {
    RayHit hit{};
    hit.hit = false;
    hit.dist = -1.0f;
    
    float tmin = 0.0f;
    float tmax = FLT_MAX;
    
    for (int i = 0; i < 3; ++i) {
        float invD = 1.0f / ray.dir[i];
        float t0 = (box.min[i] - ray.origin[i]) * invD;
        float t1 = (box.max[i] - ray.origin[i]) * invD;
        if (invD < 0.0f) std::swap(t0, t1);
        tmin = std::max(tmin, t0);
        tmax = std::min(tmax, t1);
        if (tmax < tmin) return hit;
    }
    
    hit.hit = true;
    hit.dist = tmin;
    hit.point = ray.origin + ray.dir * tmin;
    
    Vec3 center = (box.min + box.max) * 0.5f;
    Vec3 half = (box.max - box.min) * 0.5f;
    Vec3 local = hit.point - center;
    
    float bias = 1.0001f;
    hit.normal = glm::normalize(Vec3(
        static_cast<float>(static_cast<int>(local.x / half.x * bias)),
        static_cast<float>(static_cast<int>(local.y / half.y * bias)),
        static_cast<float>(static_cast<int>(local.z / half.z * bias))
    ));
    
    return hit;
}

RayHit RayVsTriangle(Ray ray, Vec3 a, Vec3 b, Vec3 c) {
    RayHit hit{};
    hit.hit = false;
    hit.dist = -1.0f;
    
    Vec3 edge1 = b - a;
    Vec3 edge2 = c - a;
    Vec3 h = glm::cross(ray.dir, edge2);
    float det = glm::dot(edge1, h);
    
    if (std::abs(det) < 1e-6f) return hit;
    
    float invDet = 1.0f / det;
    Vec3 s = ray.origin - a;
    float u = glm::dot(s, h) * invDet;
    if (u < 0.0f || u > 1.0f) return hit;
    
    Vec3 q = glm::cross(s, edge1);
    float v = glm::dot(ray.dir, q) * invDet;
    if (v < 0.0f || u + v > 1.0f) return hit;
    
    float t = glm::dot(edge2, q) * invDet;
    if (t < 0.0f) return hit;
    
    hit.hit = true;
    hit.dist = t;
    hit.point = ray.origin + ray.dir * t;
    hit.normal = glm::normalize(glm::cross(edge1, edge2));
    
    return hit;
}

RayHit RayVsSphere(Ray ray, Sphere s) {
    RayHit hit{};
    hit.hit = false;
    hit.dist = -1.0f;
    
    Vec3 oc = ray.origin - s.center;
    float a = glm::dot(ray.dir, ray.dir);
    float b = 2.0f * glm::dot(oc, ray.dir);
    float c = glm::dot(oc, oc) - s.radius * s.radius;
    float discriminant = b * b - 4 * a * c;
    
    if (discriminant < 0.0f) return hit;
    
    float t = (-b - std::sqrt(discriminant)) / (2.0f * a);
    if (t < 0.0f) {
        t = (-b + std::sqrt(discriminant)) / (2.0f * a);
        if (t < 0.0f) return hit;
    }
    
    hit.hit = true;
    hit.dist = t;
    hit.point = ray.origin + ray.dir * t;
    hit.normal = glm::normalize(hit.point - s.center);
    
    return hit;
}

RayHit RayVsPlane(Ray ray, Plane p) {
    RayHit hit{};
    hit.hit = false;
    hit.dist = -1.0f;
    
    float denom = glm::dot(p.normal, ray.dir);
    if (std::abs(denom) < 1e-6f) return hit;
    
    float t = -(glm::dot(p.normal, ray.origin) + p.d) / denom;
    if (t < 0.0f) return hit;
    
    hit.hit = true;
    hit.dist = t;
    hit.point = ray.origin + ray.dir * t;
    hit.normal = p.normal;
    
    return hit;
}

bool HitRects(Rect a, Rect b) {
    return Overlaps(a, b);
}

bool HitCircles(Vec2 c1, float r1, Vec2 c2, float r2) {
    float dx = c2.x - c1.x;
    float dy = c2.y - c1.y;
    float distSq = dx * dx + dy * dy;
    float radiusSum = r1 + r2;
    return distSq <= radiusSum * radiusSum;
}

bool HitCircleRect(Vec2 c, float r, Rect rect) {
    float closestX = std::clamp(c.x, rect.x, rect.x + rect.w);
    float closestY = std::clamp(c.y, rect.y, rect.y + rect.h);
    float dx = c.x - closestX;
    float dy = c.y - closestY;
    return (dx * dx + dy * dy) <= (r * r);
}

bool HitPointRect(Vec2 p, Rect r) {
    return Contains(r, p);
}

bool HitPointCircle(Vec2 p, Vec2 c, float r) {
    float dx = p.x - c.x;
    float dy = p.y - c.y;
    return (dx * dx + dy * dy) <= (r * r);
}

bool HitPointTriangle(Vec2 p, Vec2 a, Vec2 b, Vec2 c) {
    auto sign = [](Vec2 p1, Vec2 p2, Vec2 p3) -> float {
        return (p1.x - p3.x) * (p2.y - p3.y) - (p2.x - p3.x) * (p1.y - p3.y);
    };
    
    float d1 = sign(p, a, b);
    float d2 = sign(p, b, c);
    float d3 = sign(p, c, a);
    
    bool hasNeg = (d1 < 0) || (d2 < 0) || (d3 < 0);
    bool hasPos = (d1 > 0) || (d2 > 0) || (d3 > 0);
    
    return !(hasNeg && hasPos);
}

bool HitLines(Vec2 a1, Vec2 a2, Vec2 b1, Vec2 b2, Vec2* out) {
    float denom = (a2.x - a1.x) * (b2.y - b1.y) - (a2.y - a1.y) * (b2.x - b1.x);
    if (std::abs(denom) < 1e-10f) return false;
    
    float ua = ((b1.x - a1.x) * (b2.y - b1.y) - (b1.y - a1.y) * (b2.x - b1.x)) / denom;
    float ub = ((b1.x - a1.x) * (a2.y - a1.y) - (b1.y - a1.y) * (a2.x - a1.x)) / denom;
    
    if (ua < 0 || ua > 1 || ub < 0 || ub > 1) return false;
    
    if (out) {
        out->x = a1.x + ua * (a2.x - a1.x);
        out->y = a1.y + ua * (a2.y - a1.y);
    }
    return true;
}

}
