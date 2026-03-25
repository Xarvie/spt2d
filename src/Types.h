// spt3d/Types.h — Value types, math aliases, geometry primitives
// [THREAD: any] — All types here are plain data; safe from any thread.
//
// Single source of truth for every value type in the engine.
// No GL, no IO, no heap allocation beyond what GLM does internally.
#pragma once

#include <cstdint>
#include <cstddef>
#include <cmath>
#include <string>
#include <string_view>
#include <functional>
#include <optional>
#include <vector>

#define GLM_FORCE_CTOR_INIT
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_LEFT_HANDED
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace spt3d {

// =========================================================================
//  Math aliases
// =========================================================================
using Vec2  = glm::vec2;
using Vec3  = glm::vec3;
using Vec4  = glm::vec4;
using Quat  = glm::quat;
using Mat3  = glm::mat3;
using Mat4  = glm::mat4;
using IVec2 = glm::ivec2;
using IVec3 = glm::ivec3;
using IVec4 = glm::ivec4;

// =========================================================================
//  Inline math helpers
// =========================================================================
inline Vec2  v2(float x, float y)                    { return {x, y}; }
inline Vec3  v3(float x, float y, float z)           { return {x, y, z}; }
inline Vec4  v4(float x, float y, float z, float w)  { return {x, y, z, w}; }
inline IVec2 iv2(int x, int y)                       { return {x, y}; }
inline IVec3 iv3(int x, int y, int z)                { return {x, y, z}; }

inline Mat4 M4Id()               { return Mat4(1.0f); }
inline Mat4 M4Translate(Vec3 t)  { return glm::translate(Mat4(1.0f), t); }
inline Mat4 M4Rotate(Quat q)    { return glm::mat4_cast(q); }
inline Mat4 M4Scale(Vec3 s)     { return glm::scale(Mat4(1.0f), s); }
inline Mat4 M4Scale(float s)    { return glm::scale(Mat4(1.0f), Vec3(s)); }
inline Mat4 M4Mul(Mat4 a, Mat4 b){ return a * b; }
inline Mat4 M4TRS(Vec3 t, Quat r, Vec3 s) {
    return glm::translate(Mat4(1.0f), t) * glm::mat4_cast(r) * glm::scale(Mat4(1.0f), s);
}
inline Mat4 M4LookAt(Vec3 eye, Vec3 target, Vec3 up) { return glm::lookAtLH(eye, target, up); }
inline Mat4 M4Persp(float fovy, float aspect, float zn, float zf) { return glm::perspectiveLH_ZO(fovy, aspect, zn, zf); }
inline Mat4 M4Ortho(float l, float r, float b, float t, float zn, float zf) { return glm::orthoLH_ZO(l, r, b, t, zn, zf); }
inline Mat4 M4Inv(Mat4 m)        { return glm::inverse(m); }
inline Mat4 M4Transpose(Mat4 m)  { return glm::transpose(m); }
inline Mat3 M4ToMat3(Mat4 m)     { return Mat3(m); }
inline Vec3 M4Point(Mat4 m, Vec3 p)  { return Vec3(m * Vec4(p, 1.0f)); }
inline Vec3 M4Dir(Mat4 m, Vec3 v)    { return Vec3(m * Vec4(v, 0.0f)); }

inline Quat QId()                            { return Quat(1, 0, 0, 0); }
inline Quat QAxis(Vec3 axis, float rad)      { return glm::angleAxis(rad, glm::normalize(axis)); }
inline Quat QEuler(float p, float y, float r){ return Quat(Vec3(p, y, r)); }
inline Quat QNorm(Quat q)                   { return glm::normalize(q); }
inline Quat QSlerp(Quat a, Quat b, float t) { return glm::slerp(a, b, t); }
inline Vec3 QRotate(Quat q, Vec3 v)         { return q * v; }
inline Mat4 QToMat4(Quat q)                 { return glm::mat4_cast(q); }

// =========================================================================
//  Color (uint8 RGBA, trivially copyable, 4 bytes)
// =========================================================================
struct Color { uint8_t r = 0, g = 0, b = 0, a = 255; };
static_assert(sizeof(Color) == 4);

Vec4  ToVec4(Color c);
Vec3  ToVec3(Color c);
Color FromVec4(Vec4 v);
Color FromVec3(Vec3 v);
Vec3  ToHSV(Color c);
Color FromHSV(float h, float s, float v, float a = 1.0f);
Color FromHex(uint32_t hex);
Color CLerp(Color a, Color b, float t);
Color CAlpha(Color c, float alpha);

namespace Colors {
    inline constexpr Color White{255,255,255,255}, Black{0,0,0,255}, Blank{0,0,0,0};
    inline constexpr Color Red{230,41,55,255}, Green{0,228,48,255}, Blue{0,121,241,255};
    inline constexpr Color Yellow{253,249,0,255}, Orange{255,161,0,255}, Pink{255,109,194,255};
    inline constexpr Color Purple{200,122,255,255}, Cyan{0,200,200,255};
    inline constexpr Color Gray{130,130,130,255}, DarkGray{80,80,80,255}, LightGray{200,200,200,255};
}

// =========================================================================
//  Geometry primitives (all trivially copyable)
// =========================================================================
struct Rect   { float x=0, y=0, w=0, h=0; };
struct AABB   { Vec3 min{0}, max{0}; };
struct Ray    { Vec3 origin{0}, dir{0,0,1}; };
struct Plane  { Vec3 normal{0,1,0}; float d=0; };
struct Sphere { Vec3 center{0}; float radius=1; };
struct RayHit { bool hit=false; float dist=-1.0f; Vec3 point{0}, normal{0}; };

bool Contains(Rect r, Vec2 p);
bool Contains(AABB b, Vec3 p);
bool Overlaps(Rect a, Rect b);
bool Overlaps(AABB a, AABB b);
Rect RectIntersect(Rect a, Rect b);
AABB AABBMerge(AABB a, AABB b);
AABB AABBTransform(AABB b, Mat4 m);

RayHit RayVsAABB(Ray ray, AABB box);
RayHit RayVsTriangle(Ray ray, Vec3 a, Vec3 b, Vec3 c);
RayHit RayVsSphere(Ray ray, Sphere s);
RayHit RayVsPlane(Ray ray, Plane p);

bool HitRects(Rect a, Rect b);
bool HitCircles(Vec2 c1, float r1, Vec2 c2, float r2);
bool HitCircleRect(Vec2 c, float r, Rect rect);
bool HitPointRect(Vec2 p, Rect r);
bool HitPointCircle(Vec2 p, Vec2 c, float r);
bool HitPointTriangle(Vec2 p, Vec2 a, Vec2 b, Vec2 c);
bool HitLines(Vec2 a1, Vec2 a2, Vec2 b1, Vec2 b2, Vec2* out = nullptr);

// =========================================================================
//  Transform
// =========================================================================
struct Transform {
    Vec3 pos{0,0,0};
    Quat rot{1,0,0,0};
    Vec3 scl{1,1,1};
};

Mat4      ToMat4(Transform t);
Transform FromMat4(Mat4 m);
Transform TLerp(Transform a, Transform b, float t);

// =========================================================================
//  Camera
// =========================================================================
struct Camera3D {
    Vec3  position{0,5,-10};
    Vec3  target{0,0,0};
    Vec3  up{0,1,0};
    float fov       = 60.0f;
    float near_clip = 0.1f;
    float far_clip  = 1000.0f;
    bool  ortho     = false;
};
struct Camera2D {
    Vec2  target{0,0};
    Vec2  offset{0,0};
    float rotation = 0;
    float zoom     = 1;
};

Mat4 ViewMat(const Camera3D& c);
Mat4 ProjMat(const Camera3D& c, float aspect);
Mat4 ViewProjMat(const Camera3D& c, float aspect);
Mat4 CamMat2D(const Camera2D& c);
Ray  ScreenRay(const Camera3D& c, Vec2 screen, int sw, int sh);
Vec2 WorldToScreen3D(const Camera3D& c, Vec3 pos, int sw, int sh);
Vec3 ScreenToWorld3D(const Camera3D& c, Vec2 screen, float depth, int sw, int sh);
Vec2 ScreenToWorld2D(const Camera2D& c, Vec2 screen);
Vec2 WorldToScreen2D(const Camera2D& c, Vec2 world);

// =========================================================================
//  Render state enums (value types, zero GL dependency)
// =========================================================================
enum class BlendMode : uint8_t { Alpha, Additive, Multiply, PremulAlpha, Screen, Disabled };
enum class CullFace  : uint8_t { None, Back, Front };
enum class DepthFunc : uint8_t { Less, LEqual, Greater, GEqual, Equal, Always, Never };

struct RenderState {
    BlendMode blend       = BlendMode::Alpha;
    CullFace  cull        = CullFace::Back;
    DepthFunc depth_func  = DepthFunc::Less;
    bool      depth_write = true;
    bool      depth_test  = true;
    bool      color_write = true;
    float     line_width  = 1.0f;
};

// =========================================================================
//  Pixel format
// =========================================================================
enum class Fmt : uint8_t {
    R8, RG8, RGB8, RGBA8, SRGB8, SRGB8A8,
    R16F, RG16F, RGBA16F,
    R32F, RG32F, RGBA32F,
    R11G11B10F,
    Depth16, Depth24, D24S8, D32F,
    ETC2_RGB, ETC2_RGBA, ASTC_4x4, ASTC_8x8,
};
enum class Filter : uint8_t { Nearest, Linear, MipNearest, MipLinear };
enum class Wrap   : uint8_t { Repeat, Clamp, Mirror };

int  FmtBytesPerPixel(Fmt f);
bool FmtIsDepth(Fmt f);
bool FmtIsCompressed(Fmt f);
bool FmtIsFloat(Fmt f);

// =========================================================================
//  Resource state / Light / Sort / Stage enums
// =========================================================================
enum class ResState  : uint8_t { Loading, Ready, Failed };
enum class LightType : uint8_t { Directional, Point, Spot };
enum class SortMode  : uint8_t { None, FrontToBack, BackToFront };
enum class StageType : uint8_t { Clear, Geometry, Blit, Custom };

struct Light {
    LightType type     = LightType::Directional;
    Vec3 position      {0,10,0};
    Vec3 direction     {0,-1,0};
    Color color        = Colors::White;
    float intensity    = 1.0f;
    float range        = 50.0f;
    float inner_cone   = 30.0f;
    float outer_cone   = 45.0f;
    bool  enabled      = true;
    bool  cast_shadow  = false;
};

// =========================================================================
//  Common callbacks
// =========================================================================
using Callback   = std::function<void(bool ok)>;
using FsCallback = std::function<void(const uint8_t* data, size_t, bool success)>;

// =========================================================================
//  Input events
// =========================================================================
struct TouchEvent {
    enum Type : uint8_t { Begin, Move, End, Cancel };
    Type   type = Begin;
    int    id   = 0;
    float  x    = 0.0f;
    float  y    = 0.0f;
    float  timestamp = 0.0f;
};

struct KeyEvent {
    enum Type : uint8_t { Down, Up, Repeat };
    Type        type     = Down;
    std::string key;
    std::string code;
    int         scancode = 0;
    bool        shift    = false;
    bool        ctrl     = false;
    bool        alt      = false;
    bool        meta     = false;
    float       timestamp = 0.0f;
};

struct MouseEvent {
    enum Type : uint8_t { Down, Up, Move, Wheel };
    Type   type   = Move;
    int    button = 0;
    float  x      = 0.0f;
    float  y      = 0.0f;
    float  deltaX = 0.0f;
    float  deltaY = 0.0f;
    float  wheelX = 0.0f;
    float  wheelY = 0.0f;
    float  timestamp = 0.0f;
};

struct HttpResponse {
    int         statusCode = 0;
    std::string body;
    std::string error;
    std::string errMsg;
    bool        success = false;
};

struct HttpRequest {
    std::string url;
    std::string method = "GET";
    std::string body;
    std::vector<std::pair<std::string, std::string>> headers;
    int         timeout = 30000;
};

// =========================================================================
//  Logging (implemented in Utility.cpp)
// =========================================================================
enum class LogLvl : uint8_t { Trace, Debug, Info, Warn, Error, Fatal, None };
using LogFn = std::function<void(LogLvl, std::string_view)>;

void SetLogLevel(LogLvl lvl);
void Log(LogLvl lvl, const char* fmt, ...);
void SetLogCallback(LogFn fn);

// =========================================================================
//  Random (implemented in Random.cpp)
// =========================================================================
float Randf();
float RandRange(float min, float max);
int   Randi(int min, int max);
Vec2  RandInCircle(float radius);
Vec3  RandInSphere(float radius);
Vec3  RandOnSphere(float radius);
void  SeedRand(uint64_t seed);

} // namespace spt3d
