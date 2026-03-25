#include "Types.h"

#include <cmath>

namespace spt3d {

Vec4 ToVec4(Color c) {
    return Vec4(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f);
}

Mat4 ViewMat(const Camera3D& c) {
    return glm::lookAt(c.position, c.target, c.up);
}

Mat4 ProjMat(const Camera3D& c, float aspect) {
    if (c.ortho) {
        float size = 10.0f;
        float halfH = size;
        float halfW = size * aspect;
        return glm::ortho(-halfW, halfW, -halfH, halfH, c.near_clip, c.far_clip);
    } else {
        return glm::perspective(glm::radians(c.fov), aspect, c.near_clip, c.far_clip);
    }
}

Mat4 ViewProjMat(const Camera3D& c, float aspect) {
    return ProjMat(c, aspect) * ViewMat(c);
}

} // namespace spt3d
