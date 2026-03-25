// Spt3D.h — v3 Final Conceptual API
// C++17 · OpenGL ES 3.0 · GLSL ES 300 · Async · RAII
//
// v3 修复清单（来自 6 场景模拟推演）：
//   fix1: Stage output_bindings — stage 输出自动绑到后续 stage 的 sampler
//   fix2: Pipeline 内部 RTPool — Fx 自动借还中间 RT，用户不操心
//   fix3: ScreenTarget() — 显式表示"画到屏幕"
//   fix4: PipelineGlobal — pipeline 级全局纹理/uniform 绑定
//   fix5: BuiltinUniforms — camera/time/screen 等自动注入所有 shader
//   fix6: Per-pass RenderState — ShaderPass 可覆盖 material 的 render state
//   fix7: LightBlock — light 数据自动注入所有 shader 的 uniform block
//
#pragma once
#include <cstdint>
#include <cstddef>
#include <memory>
#include <functional>
#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <initializer_list>
#include <utility>

#include "src/core/ThreadModel.h"
#include "src/core/Signal.h"

// =========================================================================
//  GLM 数学库
// =========================================================================
#define GLM_FORCE_CTOR_INIT
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_LEFT_HANDED
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace spt3d {

// =========================================================================
//  MATH — GLM 类型别名
// =========================================================================

using Vec2 = glm::vec2;
using Vec3 = glm::vec3;
using Vec4 = glm::vec4;
using Quat = glm::quat;
using Mat3 = glm::mat3;
using Mat4 = glm::mat4;
using IVec2 = glm::ivec2;
using IVec3 = glm::ivec3;
using IVec4 = glm::ivec4;

struct Rect { float x, y, w, h; };

// 辅助函数
inline Vec2 v2(float x, float y) { return Vec2(x, y); }
inline Vec3 v3(float x, float y, float z) { return Vec3(x, y, z); }
inline Vec4 v4(float x, float y, float z, float w) { return Vec4(x, y, z, w); }
inline IVec2 iv2(int x, int y) { return IVec2(x, y); }
inline IVec3 iv3(int x, int y, int z) { return IVec3(x, y, z); }

// 矩阵辅助函数
inline Mat4 M4Id() { return Mat4(1.0f); }
inline Mat4 M4Translate(Vec3 t) { return glm::translate(Mat4(1.0f), t); }
inline Mat4 M4Rotate(Quat q) { return glm::mat4_cast(q); }
inline Mat4 M4Scale(Vec3 s) { return glm::scale(Mat4(1.0f), s); }
inline Mat4 M4Scale(float s) { return glm::scale(Mat4(1.0f), Vec3(s)); }
inline Mat4 M4Mul(Mat4 a, Mat4 b) { return a * b; }

inline Mat4 M4TRS(Vec3 t, Quat r, Vec3 s) {
    return glm::translate(Mat4(1.0f), t) * glm::mat4_cast(r) * glm::scale(Mat4(1.0f), s);
}

inline Mat4 M4LookAt(Vec3 eye, Vec3 target, Vec3 up) {
    return glm::lookAtLH(eye, target, up);
}

inline Mat4 M4Persp(float fov_y, float aspect, float near, float far) {
    return glm::perspectiveLH_ZO(fov_y, aspect, near, far);
}

inline Mat4 M4Ortho(float l, float r, float b, float t, float near, float far) {
    return glm::orthoLH_ZO(l, r, b, t, near, far);
}

// 四元数辅助函数
inline Quat QId() { return Quat(1, 0, 0, 0); }
inline Quat QAxis(Vec3 axis, float rad) { return glm::angleAxis(rad, glm::normalize(axis)); }
inline Quat QEuler(float pitch, float yaw, float roll) { return Quat(glm::vec3(pitch, yaw, roll)); }
inline Quat QNorm(Quat q) { return glm::normalize(q); }
inline Quat QConj(Quat q) { return glm::conjugate(q); }
inline Quat QSlerp(Quat a, Quat b, float t) { return glm::slerp(a, b, t); }
inline Quat QLook(Vec3 fwd, Vec3 up) { return glm::quatLookAtLH(glm::normalize(fwd), up); }
inline Vec3 QRotate(Quat q, Vec3 v) { return q * v; }
inline Mat4 QToMat4(Quat q) { return glm::mat4_cast(q); }

// 矩阵函数（GLM 不直接提供的）
inline Mat4 M4Inv(Mat4 m) { return glm::inverse(m); }
inline Mat4 M4Transpose(Mat4 m) { return glm::transpose(m); }
inline Mat3 M4ToMat3(Mat4 m) { return Mat3(m); }
inline Vec3 M4Point(Mat4 m, Vec3 p) { return Vec3(m * Vec4(p, 1.0f)); }
inline Vec3 M4Dir(Mat4 m, Vec3 v) { return Vec3(m * Vec4(v, 0.0f)); }
inline Vec4 M4Vec4(Mat4 m, Vec4 v) { return m * v; }

// =========================================================================
//  COLOR
// =========================================================================

struct Color { uint8_t r, g, b, a; };

Vec4  ToVec4(Color c);
Color FromVec4(Vec4 v);
Color FromHSV(float h, float s, float v);
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
//  GEOMETRY PRIMITIVES
// =========================================================================

struct AABB   { Vec3 min, max; };
struct Ray    { Vec3 origin, dir; };
struct Plane  { Vec3 normal; float d; };
struct Sphere { Vec3 center; float radius; };
struct RayHit { bool hit; float dist; Vec3 point, normal; };

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
bool HitLines(Vec2 a1, Vec2 a2, Vec2 b1, Vec2 b2, Vec2* out);

// =========================================================================
//  RESOURCE SYSTEM
// =========================================================================

enum class ResState : uint8_t { Loading, Ready, Failed };
using Callback = std::function<void(bool ok)>;

// -------------------------------------------------------------------------
//  ResHandle — 资源句柄（来自 spt2d slot-map 设计）
// -------------------------------------------------------------------------
//
//  优势：
//  - 防悬空指针：generation 计数器检测已释放的句柄
//  - O(1) 查找：通过 index 直接访问
//  - 内存紧凑：所有资源存储在连续数组中
//  - 批量操作：通过 tag 可以批量卸载资源
//
struct ResHandle {
    uint32_t value = 0;
    
    uint16_t Index()     const { return value & 0xFFFF; }
    uint8_t  Generation() const { return (value >> 16) & 0xFF; }
    uint8_t  Tag()       const { return (value >> 24) & 0xFF; }
    bool     Valid()     const { return value != 0; }
    
    static ResHandle Make(uint16_t idx, uint8_t gen, uint8_t tag = 0) {
        return ResHandle{(uint32_t(tag) << 24) | (uint32_t(gen) << 16) | idx};
    }
};

// -------------------------------------------------------------------------
//  ThreadModel — 线程模型配置（来自 spt2d）
// -------------------------------------------------------------------------
//
//  支持单线程和多线程两种模式：
//  - 单线程：平台事件 → 游戏逻辑 → 渲染（顺序执行）
//  - 多线程：主线程处理输入+渲染，业务线程执行游戏逻辑
//
//  多线程模式下使用 TripleBuffer<GameWork> 传递帧数据，无锁高效。
//

void SetThreadConfig(const ThreadConfig& cfg);
ThreadConfig GetThreadConfig();

// -------------------------------------------------------------------------
//  VFS — 虚拟文件系统（内部实现，最小化暴露）
// -------------------------------------------------------------------------
//
//  路径约定：
//    assets://path/to/file    → 资源目录（只读）
//    user://path/to/file      → 用户数据目录（可写）
//    cache://path/to/file     → 缓存目录（可写）
//    http://...               → 网络资源
//
//  使用流程：
//    1. Init 前调用 MountAssets/MountUser/MountCache
//    2. 使用 LoadTex/LoadModel 等高级 API（自动使用 VFS）
//    3. 或直接使用 FsRead/FsWrite 低级 API
// -------------------------------------------------------------------------

// 挂载配置（Init 前调用）
void MountAssets(std::string_view path);    // 挂载资源目录
void MountUser(std::string_view path);      // 挂载用户数据目录
void MountCache(std::string_view path);     // 挂载缓存目录

// 低级 VFS API
using FsCallback = std::function<void(const uint8_t* data, size_t size, bool success)>;
uint64_t FsRead(std::string_view path, FsCallback cb);      // 返回请求 ID
uint64_t FsWrite(std::string_view path, const uint8_t* data, size_t size, FsCallback cb);
void     FsCancel(uint64_t id);
void     FsCancelAll();

// 高级 API（内部使用 VFS）
void FetchBinary(std::string_view url, std::function<void(const uint8_t*, size_t, bool)> cb);
void FetchText(std::string_view url, std::function<void(std::string_view, bool)> cb);

// =========================================================================
//  PIXEL FORMAT
// =========================================================================

enum class Fmt : uint8_t {
    R8, RG8, RGB8, RGBA8, SRGB8, SRGB8A8,
    R16F, RG16F, RGBA16F,
    R32F, RG32F, RGBA32F,
    R11G11B10F,
    Depth16, Depth24, D24S8, D32F,
    ETC2_RGB, ETC2_RGBA, ASTC_4x4, ASTC_8x8,
};

int FmtBytesPerPixel(Fmt f);
bool FmtIsDepth(Fmt f);
bool FmtIsCompressed(Fmt f);
bool FmtIsFloat(Fmt f);

// =========================================================================
//  TEXTURE
// =========================================================================

enum class Filter : uint8_t { Nearest, Linear, MipNearest, MipLinear };
enum class Wrap   : uint8_t { Repeat, Clamp, Mirror };

struct Texture {
    struct Impl; std::shared_ptr<Impl> p;
    Texture() = default;
    ~Texture() = default;
    Texture(const Texture&) = default;
    Texture(Texture&&) noexcept = default;
    Texture& operator=(const Texture&) = default;
    Texture& operator=(Texture&&) noexcept = default;
    ResState State() const;
    bool Ready() const;
    bool Valid() const;
    int  W() const;
    int  H() const;
    Fmt  Format() const;
    uint32_t GL() const;
    explicit operator bool() const;
};

Texture LoadTex(std::string_view url, Callback cb = nullptr);
Texture LoadTexMem(const uint8_t* data, size_t sz, std::string_view hint = "", Callback cb = nullptr);
Texture CreateTex(int w, int h, Fmt fmt = Fmt::RGBA8, const void* px = nullptr);
Texture SolidTex(Color c);
Texture WhiteTex();
Texture BlackTex();
Texture NormalTex();
Texture NoiseTex(int w, int h);
void SetFilter(Texture t, Filter min_f, Filter mag_f);
void SetWrap(Texture t, Wrap u, Wrap v);
void GenMips(Texture t);
void Update(Texture t, const void* px);
void UpdateRegion(Texture t, Rect region, const void* px);

namespace detail {
    Texture CreateTexFromGL(uint32_t glTex, int w, int h, Fmt fmt);
}

// =========================================================================
//  RENDER TARGET
// =========================================================================

struct RenderTarget {
    struct Impl; std::shared_ptr<Impl> p;
    bool Valid() const;
    int  W() const;
    int  H() const;
    int  ColorCount() const;
    Texture GetColor(int index = 0) const;
    Texture GetDepth() const;
    uint32_t GL() const;
    explicit operator bool() const;
};

// [fix3] 显式屏幕目标常量
RenderTarget ScreenTarget();   // 返回空 handle = 画到屏幕默认 framebuffer

struct RTBuilder {
    RTBuilder(int w, int h);
    RTBuilder& AddColor(Fmt fmt = Fmt::RGBA8);
    RTBuilder& AddColorFiltered(Fmt fmt, Filter f);
    RTBuilder& SetDepth(Fmt fmt = Fmt::D24S8);
    RTBuilder& NoDepth();
    RTBuilder& Scale(float s);
    RenderTarget Build();
    struct Impl; std::unique_ptr<Impl> p;
};

RenderTarget CreateRT(int w, int h);
RenderTarget CreateRTHDR(int w, int h);
RenderTarget CreateRTMRT(int w, int h, std::initializer_list<Fmt> fmts);

// =========================================================================
//  RENDER STATE
// =========================================================================

enum class BlendMode : uint8_t { Alpha, Additive, Multiply, PremulAlpha, Screen, Disabled };
enum class CullFace  : uint8_t { None, Back, Front };
enum class DepthFunc : uint8_t { Less, LEqual, Greater, GEqual, Equal, Always, Never };

struct BlendEq {
    uint32_t src_rgb, dst_rgb, src_a, dst_a;
    uint32_t eq_rgb, eq_a;
};

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
//  SHADER — 多 pass + per-pass render state [fix6]
// =========================================================================
//
//  一个 Shader 包含多个 named pass。
//  Pipeline 的每个 geometry stage 指定 pass_name，renderer 从物体的
//  shader 中取同名 pass 来画。没有该 pass 的物体被跳过。
//
//  [fix6] 每个 pass 可以覆盖 material 的默认 render state。
//  用途：OUTLINE pass 需要 CullFace::Front，SHADOW pass 需要 depth bias。
//

struct ShaderPass {
    std::string name;           // "FORWARD", "SHADOW", "DEPTH", "GBUFFER", "OUTLINE", "NORMAL"...
    std::string_view vs;        // GLSL ES 300 source ("" = 内建默认)
    std::string_view fs;        // GLSL ES 300 source
    std::optional<RenderState> state;   // [fix6] 若有值则覆盖 material 的 render state
};

struct Shader {
    struct Impl; std::shared_ptr<Impl> p;
    Shader() = default;
    ~Shader() = default;
    Shader(Shader&&) noexcept = default;
    Shader& operator=(Shader&&) noexcept = default;
    ResState State() const;
    bool Ready() const;
    bool Valid() const;
    bool HasPass(std::string_view name) const;
    int  PassCount() const;
    int  GetLoc(std::string_view pass, std::string_view uniform) const;
    explicit operator bool() const;
    
    bool loadFromSource(std::string_view vertexSrc, std::string_view fragmentSrc);
    void use() const;
    int uniformLocation(std::string_view name) const;
    std::string lastError() const;
    unsigned int program() const;
    
    void setInt(std::string_view name, int value) const;
    void setFloat(std::string_view name, float value) const;
    void setVec2(std::string_view name, float x, float y) const;
    void setVec3(std::string_view name, float x, float y, float z) const;
    void setVec4(std::string_view name, float x, float y, float z, float w) const;
    void setMat4(std::string_view name, const float* matrix) const;
};

Shader CreateShader(std::initializer_list<ShaderPass> passes);
Shader LoadShader(std::string_view url, Callback cb = nullptr);
Shader CreateSimpleShader(std::string_view vs, std::string_view fs);

Shader DefaultShader();
Shader DefaultShader3D();
Shader PBRShader();
Shader UnlitShader();
Shader SpriteShader();
Shader SkyboxShader();

// =========================================================================
//  MATERIAL — uniform 快照 + tag 分类 + render state
// =========================================================================

struct Material {
    struct Impl; std::shared_ptr<Impl> p;
    Material() = default;
    ~Material() = default;
    Material(const Material&) = default;
    Material(Material&&) noexcept = default;
    Material& operator=(const Material&) = default;
    Material& operator=(Material&&) noexcept = default;
    bool Valid() const;
    Shader* GetShader() const;
    explicit operator bool() const;

    void Set(std::string_view name, int v);
    void Set(std::string_view name, float v);
    void Set(std::string_view name, Vec2 v);
    void Set(std::string_view name, Vec3 v);
    void Set(std::string_view name, Vec4 v);
    void Set(std::string_view name, Color v);
    void Set(std::string_view name, Mat3 v);
    void Set(std::string_view name, Mat4 v);
    void Set(std::string_view name, const float* data, int count);
    void SetTex(std::string_view name, Texture tex, int slot = 0);

    void SetAlbedo(Texture tex);
    void SetAlbedoColor(Color c);
    void SetNormalMap(Texture tex);
    void SetEmission(Texture tex);
    void SetEmissionStrength(float v);
    void SetMetallicRoughness(Texture tex);
    void SetMetallic(float v);
    void SetRoughness(float v);
    void SetOcclusion(Texture tex);

    void SetState(RenderState state);
    void SetBlend(BlendMode mode);
    void SetBlendEq(BlendEq eq);
    void SetCull(CullFace face);
    void SetDepthWrite(bool w);
    void SetDepthTest(bool t);

    void SetTag(std::string_view tag);
    std::string_view GetTag() const;
};

Material CreateMat(Shader shader);
Material DefaultMat();
Material UnlitMat();
Material PBRMat();
Material CloneMat(Material src);

// =========================================================================
//  MESH
// =========================================================================

struct Mesh {
    struct Impl; std::shared_ptr<Impl> p;
    ResState State() const;
    bool Ready() const;
    bool Valid() const;
    int  Verts() const;
    int  Indices() const;
    AABB Bounds() const;
    unsigned int GL() const;
    explicit operator bool() const;
};

struct MeshBuilder {
    MeshBuilder();
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
    Mesh Build();
    struct Impl; std::unique_ptr<Impl> p;
};

void UpdatePos(Mesh m, const Vec3* data, int n, int off = 0);
void UpdateIdx(Mesh m, const uint16_t* data, int n, int off = 0);
void UpdateUV(Mesh m, const Vec2* data, int n, int off = 0);

Mesh GenPlane(float w, float h, int sx = 1, int sz = 1);
Mesh GenCube(float w, float h, float d);
Mesh GenSphere(float r, int rings = 16, int slices = 16);
Mesh GenHemiSphere(float r, int rings = 16, int slices = 16);
Mesh GenCylinder(float r, float h, int slices = 16);
Mesh GenCone(float r, float h, int slices = 16);
Mesh GenTorus(float r, float tube, int rseg = 24, int tseg = 12);
Mesh GenCapsule(float r, float h, int rings = 8, int slices = 16);
Mesh GenFullscreenTri();

// =========================================================================
//  MODEL + ANIMATION
// =========================================================================

struct Model {
    struct Impl; std::shared_ptr<Impl> p;
    ResState State() const;
    bool Ready() const;
    bool Valid() const;
    int  MeshCount() const;
    int  MatCount() const;
    Mesh GetMesh(int i) const;
    Material GetMat(int i) const;
    void SetMat(int mesh_idx, Material mat);
    AABB Bounds() const;
    explicit operator bool() const;
};

Model LoadModel(std::string_view url, Callback cb = nullptr);
Model MakeModel(Mesh mesh, Material mat);
Model MakeModel(std::initializer_list<std::pair<Mesh, Material>> parts);

struct Animation {
    struct Impl; std::shared_ptr<Impl> p;
    bool Valid() const;
    float Duration() const;
    std::string_view Name() const;
};

std::vector<Animation> LoadAnims(std::string_view url, Callback cb = nullptr);
void Animate(Model model, Animation anim, float time);
void AnimateBlend(Model model, Animation a, Animation b, float weight);

// =========================================================================
//  TRANSFORM
// =========================================================================

struct Transform {
    Vec3 pos = {0,0,0};
    Quat rot = {0,0,0,1};
    Vec3 scl = {1,1,1};
};

Mat4 ToMat4(Transform t);
Transform FromMat4(Mat4 m);
Transform TLerp(Transform a, Transform b, float t);

// =========================================================================
//  CAMERA
// =========================================================================

struct Camera2D {
    Vec2  target   = {0,0};
    Vec2  offset   = {0,0};
    float rotation = 0;
    float zoom     = 1;
};

struct Camera3D {
    Vec3  position  = {0,5,-10};
    Vec3  target    = {0,0,0};
    Vec3  up        = {0,1,0};
    float fov       = 60;
    float near_clip = 0.1f;
    float far_clip  = 1000.0f;
    bool  ortho     = false;
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
//  LIGHT [fix7]
// =========================================================================
//
//  Light 数据以 uniform block 形式自动注入所有 shader：
//
//    uniform int       u_lightCount;
//    uniform vec3      u_ambientColor;
//    uniform float     u_ambientIntensity;
//
//    struct LightData {
//        int   type;         // 0=dir, 1=point, 2=spot
//        vec3  position;
//        vec3  direction;
//        vec3  color;
//        float intensity;
//        float range;
//        float innerCone;
//        float outerCone;
//        bool  shadow;
//    };
//    uniform LightData u_lights[MAX_LIGHTS];  // MAX_LIGHTS = 16
//
//  用户 shader 直接 for(int i=0; i<u_lightCount; i++) 遍历即可。
//

enum class LightType : uint8_t { Directional, Point, Spot };

struct Light {
    LightType type     = LightType::Directional;
    Vec3 position      = {0,10,0};
    Vec3 direction     = {0,-1,0};
    Color color        = Colors::White;
    float intensity    = 1.0f;
    float range        = 50.0f;
    float inner_cone   = 30.0f;
    float outer_cone   = 45.0f;
    bool  enabled      = true;
    bool  cast_shadow  = false;
};

int  AddLight(Light light);
void SetLight(int idx, Light light);
void RemoveLight(int idx);
Light GetLight(int idx);
int  LightCount();
void SetAmbient(Color color, float intensity = 0.1f);
void SetShadowMapSize(int size);

// =========================================================================
//  ENVIRONMENT
// =========================================================================

void SetSkybox(Texture cubemap);
void SetEnvMap(Texture cubemap);
void SetFog(Color color, float start, float end);
void SetFogExp(Color color, float density);
void DisableFog();

// =========================================================================
//  BUILTIN UNIFORMS [fix5]
// =========================================================================
//
//  Execute() 自动上传以下 uniform 到所有 shader 的所有 pass。
//  用户 shader 里直接声明即可使用，不需要手动 Set()。
//
//  // ---- per-frame (camera/time) ----
//  uniform mat4  u_view;
//  uniform mat4  u_projection;
//  uniform mat4  u_viewProj;
//  uniform vec3  u_cameraPos;
//  uniform vec3  u_cameraDir;
//  uniform vec2  u_screenSize;      // render width/height in pixels
//  uniform vec2  u_screenSizeInv;   // 1.0 / screenSize
//  uniform float u_time;            // seconds since Init()
//  uniform float u_deltaTime;       // seconds since last frame
//  uniform float u_nearClip;
//  uniform float u_farClip;
//
//  // ---- per-draw (transform) ----
//  uniform mat4  u_model;
//  uniform mat4  u_modelView;
//  uniform mat4  u_mvp;
//  uniform mat3  u_normalMatrix;
//
//  // ---- light block (see LIGHT section) ----
//  // ---- pipeline globals (see PipelineGlobal below) ----
//

// =========================================================================
//  DRAW LIST
// =========================================================================

struct DrawItem {
    Mesh     mesh;
    Material mat;
    Mat4     transform;
    float    sort_key = 0;   // 0 = 自动按距离排序
};

struct DrawList {
    struct Impl; std::shared_ptr<Impl> p;
};

DrawList CreateDrawList();
void     Clear(DrawList dl);
void     Push(DrawList dl, DrawItem item);
void     Push(DrawList dl, Mesh mesh, Material mat, Mat4 transform);
void     Push(DrawList dl, Mesh mesh, Material mat, Transform transform);
void     Push(DrawList dl, Model model, Transform transform);
void     Push(DrawList dl, Model model, Mat4 transform);
int      Count(DrawList dl);

// -------------------------------------------------------------------------
//  GameWork — 帧数据包（来自 spt2d）
// -------------------------------------------------------------------------
//
//  在多线程模式下，业务线程将渲染命令录制到 GameWork，
//  然后通过 TripleBuffer 传递到主线程执行。
//
//  用户通常不需要直接操作 GameWork，使用 DrawList 或即时模式 API 即可。
//

struct GameWork;

// 获取当前帧的 GameWork（用于高级定制）
GameWork& CurrentWork();

// =========================================================================
//  RENDER PIPELINE — 核心
// =========================================================================

enum class StageType : uint8_t {
    Clear,       // 清除 target
    Geometry,    // 画 DrawList 中匹配的几何体
    Blit,        // 全屏 blit（后处理核心）
    Custom,      // 用户回调
};

enum class SortMode : uint8_t {
    None,
    FrontToBack,
    BackToFront,
};

// [fix1] output binding: 把 stage 的 target attachment 绑到一个 sampler 名
//        后续所有 stage 的 shader 自动可见该 sampler
struct OutputBinding {
    std::string sampler_name;    // uniform sampler2D 名，e.g. "u_depthTex"
    int attachment_index = 0;    // target 的第几个 color attachment
};

struct StageDesc {
    std::string name;
    StageType   type = StageType::Geometry;

    // ---- target ----
    RenderTarget target;                // 空/ScreenTarget() = 屏幕

    // ---- clear ----
    bool  clear_color = false;
    bool  clear_depth = false;
    Color clear_color_value = Colors::Black;
    float clear_depth_value = 1.0f;

    // ---- geometry stage ----
    std::string pass_name;              // shader pass name
    std::string tag_include;            // 只画带此 tag 的物体（"" = 全部）
    std::string tag_exclude;            // 排除带此 tag 的物体
    SortMode    sort = SortMode::FrontToBack;
    std::optional<RenderState> state_override;   // stage 级别的 render state 覆盖

    // ---- blit stage ----
    Material blit_material;
    std::vector<Texture> blit_inputs;
    
    void AddBlitInput(Texture tex) { blit_inputs.push_back(std::move(tex)); }

    // ---- custom stage ----
    std::function<void()> custom_fn;

    // ---- [fix1] output bindings ----
    std::vector<OutputBinding> outputs;
};

// Pipeline
struct Pipeline {
    struct Impl; std::shared_ptr<Impl> p;
};

Pipeline CreatePipeline();
void     PushStage(Pipeline pipe, StageDesc stage);
void     InsertStage(Pipeline pipe, int index, StageDesc stage);
void     RemoveStage(Pipeline pipe, int index);
void     RemoveStage(Pipeline pipe, std::string_view name);
void     EnableStage(Pipeline pipe, std::string_view name, bool on);
bool     StageEnabled(Pipeline pipe, std::string_view name);
int      StageCount(Pipeline pipe);
StageDesc& GetStage(Pipeline pipe, int index);
StageDesc& GetStage(Pipeline pipe, std::string_view name);

// [fix4] Pipeline 全局绑定 — 手动把纹理/uniform 绑到所有 stage
void PipelineBindTex(Pipeline pipe, std::string_view sampler, Texture tex, int slot);
void PipelineBindFloat(Pipeline pipe, std::string_view name, float value);
void PipelineBindVec2(Pipeline pipe, std::string_view name, Vec2 value);
void PipelineBindVec3(Pipeline pipe, std::string_view name, Vec3 value);
void PipelineBindVec4(Pipeline pipe, std::string_view name, Vec4 value);
void PipelineBindMat4(Pipeline pipe, std::string_view name, Mat4 value);
void PipelineClearBindings(Pipeline pipe);

// [fix2] Execute 内部维护帧级 RT pool，Fx stage 自动借还中间 RT
//        用户完全不需要操心中间 RT
void Execute(Pipeline pipe, DrawList dl, const Camera3D& cam);
void Execute(Pipeline pipe, DrawList dl, const Camera2D& cam);

// =========================================================================
//  PREDEFINED PIPELINES
// =========================================================================

Pipeline ForwardPipeline();           // clear → FORWARD opaque → FORWARD transparent
Pipeline ForwardHDRPipeline();        // 同上 + bloom + tonemap
Pipeline DeferredPipeline();          // GBuffer → light pass → transparent → tonemap
Pipeline ToonPipeline();              // FORWARD → OUTLINE → screen

// =========================================================================
//  BLIT
// =========================================================================

void Blit(Texture src, RenderTarget dst, Material mat);
void Blit(Texture src, RenderTarget dst);
void BlitToScreen(Texture src);
void BlitToScreen(Texture src, Material mat);
void BlitToScreen(Texture src, Rect dst_rect);
void BlitMulti(std::initializer_list<Texture> inputs, RenderTarget dst, Material mat);

// =========================================================================
//  POST EFFECTS — 返回 StageDesc，可直接 PushStage 到 pipeline
// =========================================================================
//
//  [fix2] 所有 Fx 内部需要的中间 RT 由 Execute 的帧 RT pool 自动管理。
//  src/dst 参数只指定输入输出端点。
//

StageDesc FxBloom(Texture src, RenderTarget dst, float threshold = 1.0f, float intensity = 1.0f, int iterations = 5);
StageDesc FxToneMapACES(Texture src, RenderTarget dst = ScreenTarget());
StageDesc FxToneMapReinhard(Texture src, RenderTarget dst = ScreenTarget());
StageDesc FxToneMapExposure(Texture src, RenderTarget dst = ScreenTarget(), float exposure = 1.0f);
StageDesc FxFXAA(Texture src, RenderTarget dst = ScreenTarget());
StageDesc FxSSAO(Texture depth_tex, Texture normal_tex, RenderTarget dst, int samples = 16, float radius = 0.5f);
StageDesc FxVignette(Texture src, RenderTarget dst = ScreenTarget(), float intensity = 0.5f);
StageDesc FxChromatic(Texture src, RenderTarget dst = ScreenTarget(), float offset = 0.005f);
StageDesc FxGrain(Texture src, RenderTarget dst = ScreenTarget(), float amount = 0.05f);
StageDesc FxGaussBlur(Texture src, RenderTarget dst, float radius = 4.0f);
StageDesc FxKawaseBlur(Texture src, RenderTarget dst, int iterations = 3);
StageDesc FxPixelate(Texture src, RenderTarget dst = ScreenTarget(), int block = 4);
StageDesc FxOutline(Texture depth_tex, RenderTarget dst, Color color = Colors::Black, float thickness = 1.0f);
StageDesc FxColorGrade(Texture src, RenderTarget dst = ScreenTarget(), Texture lut = {});
StageDesc FxCustom(Texture src, RenderTarget dst, Material mat);
StageDesc FxCustomMulti(std::initializer_list<Texture> inputs, RenderTarget dst, Material mat);

// 也可以只拿 material 用于手动 Blit
Material BloomMat(float threshold = 1.0f, float intensity = 1.0f);
Material ToneMapACESMat();
Material ToneMapReinhardMat();
Material FXAAMat();
Material SSAOMat(int samples = 16, float radius = 0.5f);
Material OutlineMat(Color color = Colors::Black, float thickness = 1.0f);
Material GaussBlurMat(float radius = 4.0f);

// =========================================================================
//  FONT
// =========================================================================

struct Font {
    struct Impl; std::shared_ptr<Impl> p;
    ResState State() const;
    bool Ready() const;
    bool Valid() const;
    float LineH(float size) const;
    Vec2  Measure(std::string_view text, float size, float spacing = 0) const;
    Texture Atlas() const;
    explicit operator bool() const;
};

Font LoadFont(std::string_view url, float base_size = 48, Callback cb = nullptr);
Font LoadFontMem(const uint8_t* data, size_t sz, float base_size = 48, Callback cb = nullptr);
Font DefaultFont();

// =========================================================================
//  AUDIO
// =========================================================================

struct Sound {
    struct Impl; std::shared_ptr<Impl> p;
    ResState State() const; bool Ready() const; bool Valid() const;
    float Duration() const;
    explicit operator bool() const;
};

struct Music {
    struct Impl; std::shared_ptr<Impl> p;
    ResState State() const; bool Ready() const; bool Valid() const;
    float Duration() const; float Played() const;
    explicit operator bool() const;
};

Sound LoadSound(std::string_view url, Callback cb = nullptr);
Music LoadMusic(std::string_view url, Callback cb = nullptr);

void Play(Sound s);  void Stop(Sound s);  void Pause(Sound s);  void Resume(Sound s);
bool Playing(Sound s);
void SetVolume(Sound s, float v);
void SetPitch(Sound s, float p);
void SetPan(Sound s, float pan);

void Play(Music m);  void Stop(Music m);  void Pause(Music m);  void Resume(Music m);
void Seek(Music m, float sec);
bool Playing(Music m);
void SetVolume(Music m, float v);
void SetPitch(Music m, float p);
void SetLooping(Music m, bool loop);

void SetMasterVolume(float v);
float GetMasterVolume();
void ResumeAudio();

// =========================================================================
//  LIFECYCLE
// =========================================================================

struct AppConfig {
    std::string_view title  = "SptGame";
    int width = 800, height = 600;
    bool high_dpi   = true;
    bool resizable  = true;
    bool vsync      = true;
    int  msaa       = 0;
    Color clear     = Colors::Black;
    std::string_view canvas = "#canvas";
};

// -------------------------------------------------------------------------
//  IGameLogic — 游戏逻辑接口
// -------------------------------------------------------------------------
//
//  在多线程模式下：
//  - onInit/onUpdate/onRender 在业务线程执行
//  - 主线程只负责输入处理和渲染提交
//
//  在单线程模式下：
//  - 所有方法在主线程顺序执行
//

// 方式 1：使用 IGameLogic 接口（推荐用于多线程）
bool Init(const AppConfig& cfg, std::unique_ptr<IGameLogic> game);
void Run();

// 方式 2：使用回调函数（简单场景）
bool Init(const AppConfig& cfg = {});
void Run(std::function<void()> tick, int target_fps = 0);

// 通用控制
void Shutdown();
void BeginFrame();
void EndFrame();
void Quit();
bool ShouldQuit();

// =========================================================================
//  WINDOW / CANVAS
// =========================================================================

int   ScreenW();
int   ScreenH();
int   RenderW();
int   RenderH();
float DPR();
void  Resize(int w, int h);
bool  Resized();
bool  Focused();
bool  IsFullscreen();
void  ToggleFullscreen();

// -------------------------------------------------------------------------
//  Signal — 信号/槽事件系统（来自 spt2d）
// -------------------------------------------------------------------------
//
//  用法：
//    auto conn = OnResize.Connect([](int w, int h) { ... });
//    conn.Disconnect();  // 手动断开
//    // 或离开作用域自动断开
//

// 平台事件信号
Signal<int, int>& OnResize();
Signal<>& OnFocusGain();
Signal<>& OnFocusLost();
Signal<>& OnQuit();
Signal<std::string_view>& OnDropFile();

// =========================================================================
//  TIMING
// =========================================================================

float Delta();
double Time();
int   FPS();
void  SetTargetFPS(int fps);

// =========================================================================
//  TEXT
// =========================================================================

void DrawText(std::string_view text, Vec2 pos, float size, Color c = Colors::White);
void DrawText(Font font, std::string_view text, Vec2 pos, float size, float spacing = 0, Color c = Colors::White);
void DrawTextPro(Font font, std::string_view text, Vec2 pos, Vec2 origin, float rot, float size, float spacing, Color c = Colors::White);
void DrawText3D(Font font, std::string_view text, Vec3 pos, float size, Color c = Colors::White);
Vec2 MeasureText(std::string_view text, float size, float spacing = 0);
Vec2 MeasureText(Font font, std::string_view text, float size, float spacing = 0);

// =========================================================================
//  INPUT — KEYBOARD
// =========================================================================

enum class Key : int {
    Unknown=0, Space=32, Apostrophe=39, Comma=44, Minus=45, Period=46, Slash=47,
    D0=48,D1,D2,D3,D4,D5,D6,D7,D8,D9, Semicolon=59, Equal=61,
    A=65,B,C,D,E,F,G,H,I,J,K,L,M,N,O,P,Q,R,S,T,U,V,W,X,Y,Z,
    LeftBracket=91,Backslash=92,RightBracket=93,Grave=96,
    Escape=256,Enter,Tab,Backspace,Insert,Delete,Right,Left,Down,Up,
    PageUp,PageDown,Home,End,
    CapsLock=280,ScrollLock,NumLock,PrintScreen,PauseKey,
    F1=290,F2,F3,F4,F5,F6,F7,F8,F9,F10,F11,F12,
    LShift=340,LCtrl,LAlt,LSuper,RShift,RCtrl,RAlt,RSuper,
};

bool KeyDown(Key k);
bool KeyPressed(Key k);
bool KeyRepeat(Key k);
bool KeyReleased(Key k);
bool KeyUp(Key k);
Key  GetKeyPressed();
int  GetCharPressed();
bool Shift();
bool Ctrl();
bool Alt();
bool Meta();

// =========================================================================
//  INPUT — MOUSE
// =========================================================================

enum class Btn : uint8_t { Left=0, Right=1, Middle=2 };

bool BtnDown(Btn b);
bool BtnPressed(Btn b);
bool BtnReleased(Btn b);
Vec2 MousePos();
Vec2 MouseDelta();
float Wheel();
Vec2  WheelV();
void SetMouse(int x, int y);
void SetCursor(int cursor_id);
void ShowCursor();
void HideCursor();
bool CursorHidden();

// =========================================================================
//  INPUT — TOUCH / POINTER (unified)
// =========================================================================

int  TouchCount();
Vec2 TouchPos(int i = 0);
int  TouchId(int i);
Vec2 PointerPos(int id = 0);
Vec2 PointerDelta(int id = 0);
bool PointerDown(int id = 0);
bool PointerPressed(int id = 0);
bool PointerReleased(int id = 0);
float PointerPressure(int id = 0);
int  PointerCount();

// =========================================================================
//  INPUT — GAMEPAD
// =========================================================================

enum class GpBtn : uint8_t {
    A=0,B,X,Y,LB,RB,LT,RT,Select,Start,LS,RS,Up,Down,Left,Right,Home
};
enum class GpAxis : uint8_t { LX=0,LY,RX,RY };

bool GpAvail(int i = 0);
std::string_view GpName(int i = 0);
bool  GpDown(int i, GpBtn b);
bool  GpPressed(int i, GpBtn b);
bool  GpReleased(int i, GpBtn b);
float GpAxisVal(int i, GpAxis a);
void  GpVibrate(int i, float weak, float strong, float dur = 0.2f);

// =========================================================================
//  INPUT — GESTURE
// =========================================================================

enum class Gesture : uint32_t {
    None=0, Tap=1, DoubleTap=2, Hold=4, Drag=8,
    SwipeR=16, SwipeL=32, SwipeU=64, SwipeD=128, PinchIn=256, PinchOut=512,
};

void    SetGestures(uint32_t flags);
Gesture DetectedGesture();
float   GestureHoldDuration();
Vec2    GestureDragVec();
float   GestureDragAngle();
Vec2    GesturePinchVec();
float   GesturePinchAngle();

// =========================================================================
//  GPU CAPS
// =========================================================================

struct GPUCaps {
    int  max_tex_size;
    int  max_tex_units;
    int  max_color_attachments;
    int  max_draw_buffers;
    int  max_vertex_attribs;
    int  max_uniform_block_size;
    bool float_tex;
    bool float_linear;
    bool color_buf_float;
    bool etc2, astc;
    bool instancing;
    bool depth_tex;
    bool srgb;
    std::string_view renderer;
    std::string_view vendor;
    std::string_view version;
};

GPUCaps GetCaps();

// =========================================================================
//  UTILITY
// =========================================================================

enum class LogLvl : uint8_t { Trace, Debug, Info, Warn, Error, Fatal, None };
void SetLogLevel(LogLvl lvl);
void Log(LogLvl lvl, const char* fmt, ...);
using LogFn = std::function<void(LogLvl, std::string_view)>;
void SetLogCallback(LogFn fn);

// GL Error checking (debug only)
#ifdef _DEBUG
#define GL_CHECK(call) do { \
    call; \
    GLenum err = glGetError(); \
    if (err != GL_NO_ERROR) { \
        spt3d::Log(spt3d::LogLvl::Error, "[GL Error] 0x%X at %s:%d", err, __FILE__, __LINE__); \
    } \
} while(0)
#else
#define GL_CHECK(call) call
#endif

#define GL_CHECK_ERROR() do { \
    GLenum err = glGetError(); \
    if (err != GL_NO_ERROR) { \
        spt3d::Log(spt3d::LogLvl::Error, "[GL Error] 0x%X at %s:%d", err, __FILE__, __LINE__); \
    } \
} while(0)

void Clipboard(std::string_view text);
void GetClipboard(std::function<void(std::string_view, bool)> cb);
void OpenURL(std::string_view url);
void Screenshot(std::string_view file = "screenshot.png");

// random
float Randf();                      // [0, 1)
float Randf(float min, float max);
int   Randi(int min, int max);
Vec2  RandInCircle(float radius);
Vec3  RandInSphere(float radius);
Vec3  RandOnSphere(float radius);
void  SeedRand(uint64_t seed);

// =========================================================================
//  BUILTIN SHADER SOURCE — GLSL ES 300
// =========================================================================

extern const char* kBlitVS;
extern const char* kBlitFS;
extern const char* kSpriteVS;
extern const char* kSpriteFS;
extern const char* kUnlitVS;
extern const char* kUnlitFS;
extern const char* kPhongVS;
extern const char* kPhongFS;
extern const char* kPBR_VS;
extern const char* kPBR_FS;
extern const char* kDepthVS;
extern const char* kDepthFS;
extern const char* kNormalVS;
extern const char* kNormalFS;
extern const char* kGBufferVS;
extern const char* kGBufferFS;
extern const char* kShadowVS;
extern const char* kShadowFS;
extern const char* kSkyboxVS;
extern const char* kSkyboxFS;
extern const char* kOutlineVS;
extern const char* kOutlineFS;
extern const char* kDeferredLightFS;

} // namespace spt3d
