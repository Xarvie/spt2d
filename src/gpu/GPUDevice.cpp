#include "GPUDevice.h"
#include "../core/GameWork.h"
#include "../core/RenderCommand.h"
#include "../resource/MaterialSnapshot.h"
#include "../glad/glad.h"

#include <cstring>
#include <algorithm>
#include <unordered_map>

namespace spt3d {

static GLenum ToGLFormat(Fmt fmt) {
    switch (fmt) {
        case Fmt::RGBA8:    return GL_RGBA;
        case Fmt::RGB8:     return GL_RGB;
        case Fmt::RG8:      return GL_RG;
        case Fmt::R8:       return GL_RED;
        case Fmt::RGBA16F:  return GL_RGBA;
        case Fmt::RGBA32F:  return GL_RGBA;
        case Fmt::R16F:     return GL_RED;
        case Fmt::R32F:     return GL_RED;
        case Fmt::Depth16:  return GL_DEPTH_COMPONENT;
        case Fmt::D24S8:    return GL_DEPTH_STENCIL;
        case Fmt::D32F:     return GL_DEPTH_COMPONENT;
        default:            return GL_RGBA;
    }
}

static GLenum ToGLInternalFormat(Fmt fmt) {
    switch (fmt) {
        case Fmt::RGBA8:    return GL_RGBA8;
        case Fmt::RGB8:     return GL_RGB8;
        case Fmt::RG8:      return GL_RG8;
        case Fmt::R8:       return GL_R8;
        case Fmt::RGBA16F:  return GL_RGBA16F;
        case Fmt::RGBA32F:  return GL_RGBA32F;
        case Fmt::R16F:     return GL_R16F;
        case Fmt::R32F:     return GL_R32F;
        case Fmt::Depth16:  return GL_DEPTH_COMPONENT16;
        case Fmt::D24S8:    return GL_DEPTH24_STENCIL8;
        case Fmt::D32F:     return GL_DEPTH_COMPONENT32F;
        default:            return GL_RGBA8;
    }
}

static GLenum ToGLType(Fmt fmt) {
    switch (fmt) {
        case Fmt::RGBA16F:
        case Fmt::RGBA32F:
        case Fmt::R16F:
        case Fmt::R32F:
        case Fmt::D32F:
            return GL_FLOAT;
        case Fmt::D24S8:
            return GL_UNSIGNED_INT_24_8;
        default:
            return GL_UNSIGNED_BYTE;
    }
}

static GLenum ToGLFilter(Filter f) {
    switch (f) {
        case Filter::Nearest:    return GL_NEAREST;
        case Filter::MipNearest: return GL_NEAREST_MIPMAP_NEAREST;
        case Filter::MipLinear:  return GL_LINEAR_MIPMAP_LINEAR;
        default:                 return GL_LINEAR;
    }
}

static GLenum ToGLWrap(Wrap w) {
    switch (w) {
        case Wrap::Repeat: return GL_REPEAT;
        case Wrap::Mirror: return GL_MIRRORED_REPEAT;
        default:           return GL_CLAMP_TO_EDGE;
    }
}

static GLenum ToGLBlendMode(BlendMode mode) {
    return GL_FUNC_ADD;
}

static void ApplyBlendMode(BlendMode mode) {
    switch (mode) {
        case BlendMode::Alpha:
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            break;
        case BlendMode::Additive:
            glBlendFunc(GL_SRC_ALPHA, GL_ONE);
            break;
        case BlendMode::Multiply:
            glBlendFunc(GL_DST_COLOR, GL_ONE_MINUS_SRC_ALPHA);
            break;
        case BlendMode::PremulAlpha:
            glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            break;
        case BlendMode::Screen:
            glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_COLOR);
            break;
        case BlendMode::Disabled:
            break;
    }
}

static GLenum ToGLCullFace(CullFace c) {
    switch (c) {
        case CullFace::Front: return GL_FRONT;
        case CullFace::Back:  return GL_BACK;
        default:              return GL_FRONT_AND_BACK;
    }
}

static GLenum ToGLDepthFunc(DepthFunc f) {
    switch (f) {
        case DepthFunc::Never:    return GL_NEVER;
        case DepthFunc::Less:     return GL_LESS;
        case DepthFunc::Equal:    return GL_EQUAL;
        case DepthFunc::LEqual:   return GL_LEQUAL;
        case DepthFunc::Greater:  return GL_GREATER;
        case DepthFunc::GEqual:   return GL_GEQUAL;
        case DepthFunc::Always:   return GL_ALWAYS;
        default:                  return GL_LESS;
    }
}

struct MeshResource {
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ibo = 0;
    int    vertexCount = 0;
    int    indexCount = 0;
    bool   indexed = false;
    bool   use32bit = false;
};

struct TextureResource {
    GLuint id = 0;
    int    width = 0;
    int    height = 0;
    Fmt    format = Fmt::RGBA8;
};

struct ShaderPass {
    GLuint program = 0;
    RenderState state;
    std::unordered_map<std::string, GLint> uniformLocs;
    std::unordered_map<int32_t, GLint> uniformHashLocs;
};

struct ShaderResource {
    std::vector<ShaderPass> passes;
    std::unordered_map<std::string, int> passIndex;
};

struct RenderTargetResource {
    GLuint fbo = 0;
    GLuint depthRb = 0;
    std::vector<TexHandle> colorTextures;
    TexHandle depthTexture;
    int    width = 0;
    int    height = 0;
};

struct GPUDevice::Impl {
    GPUCaps caps;
    SlotMap<MeshResource> meshes;
    SlotMap<TextureResource> textures;
    SlotMap<ShaderResource> shaders;
    SlotMap<RenderTargetResource> renderTargets;

    TexHandle  whiteTex;
    TexHandle  blackTex;
    TexHandle  normalTex;
    MeshHandle fullscreenTri;

    GLuint boundVAO = 0;
    GLuint boundProgram = 0;
    GLuint boundFBO = 0;
    int    activeTexSlot = 0;

    RenderState currentState;
    bool        stateDirty = true;
};

GPUDevice::GPUDevice() : m_impl(new Impl()) {}
GPUDevice::~GPUDevice() { delete m_impl; }

bool GPUDevice::initialize() {
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &m_impl->caps.maxTexSize);
    glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &m_impl->caps.maxTexUnits);
    glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS, &m_impl->caps.maxColorAttachments);
    glGetIntegerv(GL_MAX_DRAW_BUFFERS, &m_impl->caps.maxDrawBuffers);
    glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &m_impl->caps.maxVertexAttribs);
    glGetIntegerv(GL_MAX_UNIFORM_BLOCK_SIZE, &m_impl->caps.maxUniformBlockSize);

    const char* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    const char* vendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
    const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));

    m_impl->caps.renderer = renderer ? renderer : "";
    m_impl->caps.vendor = vendor ? vendor : "";
    m_impl->caps.version = version ? version : "";
    m_impl->caps.instancing = true;
    m_impl->caps.depthTex = true;
    m_impl->caps.srgb = true;

    {
        TexDesc desc{1, 1, Fmt::RGBA8, Filter::Nearest, Filter::Nearest};
        uint32_t white = 0xFFFFFFFFu;
        m_impl->whiteTex = createTexture(desc, &white);

        uint32_t black = 0x000000FFu;
        m_impl->blackTex = createTexture(desc, &black);

        uint32_t normal = 0x8080FFFFu;
        m_impl->normalTex = createTexture(desc, &normal);
    }

    {
        MeshData data = GenFullscreenTriData();
        m_impl->fullscreenTri = createMesh(data);
    }

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    m_impl->currentState.depth_test = true;
    m_impl->currentState.depth_write = true;
    m_impl->currentState.cull = CullFace::Back;

    return true;
}

void GPUDevice::shutdown() {
    m_impl->meshes.forEach([](MeshResource& res) {
        if (res.vao) glDeleteVertexArrays(1, &res.vao);
        if (res.vbo) glDeleteBuffers(1, &res.vbo);
        if (res.ibo) glDeleteBuffers(1, &res.ibo);
    });

    m_impl->textures.forEach([](TextureResource& res) {
        if (res.id) glDeleteTextures(1, &res.id);
    });

    m_impl->shaders.forEach([](ShaderResource& res) {
        for (auto& pass : res.passes) {
            if (pass.program) glDeleteProgram(pass.program);
        }
    });

    m_impl->renderTargets.forEach([](RenderTargetResource& res) {
        if (res.fbo) glDeleteFramebuffers(1, &res.fbo);
        if (res.depthRb) glDeleteRenderbuffers(1, &res.depthRb);
    });

    m_impl->whiteTex = TexHandle{};
    m_impl->blackTex = TexHandle{};
    m_impl->normalTex = TexHandle{};
    m_impl->fullscreenTri = MeshHandle{};
}

const GPUCaps& GPUDevice::caps() const noexcept {
    return m_impl->caps;
}

MeshHandle GPUDevice::createMesh(const MeshData& data) {
    MeshResource res;

    glGenVertexArrays(1, &res.vao);
    glGenBuffers(1, &res.vbo);

    glBindVertexArray(res.vao);
    glBindBuffer(GL_ARRAY_BUFFER, res.vbo);

    GLsizeiptr vboSize = static_cast<GLsizeiptr>(data.vertices.size()) * sizeof(float);
    glBufferData(GL_ARRAY_BUFFER, vboSize, data.vertices.data(),
                 data.dynamic ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);

    const GLsizei stride = MeshData::kBytesPerVertex;

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(0));

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(12));

    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(24));

    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(32));

    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(48));

    res.vertexCount = data.vertexCount;

    if (data.indexCount > 0) {
        glGenBuffers(1, &res.ibo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, res.ibo);

        GLsizeiptr iboSize;
        const void* idxData;

        if (data.use32bitIndices) {
            iboSize = static_cast<GLsizeiptr>(data.indices32.size()) * sizeof(uint32_t);
            idxData = data.indices32.data();
            res.use32bit = true;
        } else {
            iboSize = static_cast<GLsizeiptr>(data.indices16.size()) * sizeof(uint16_t);
            idxData = data.indices16.data();
            res.use32bit = false;
        }

        glBufferData(GL_ELEMENT_ARRAY_BUFFER, iboSize, idxData, GL_STATIC_DRAW);
        res.indexCount = data.indexCount;
        res.indexed = true;
    } else {
        res.indexed = false;
        res.indexCount = 0;
    }

    glBindVertexArray(0);

    MeshHandle h;
    h.value = m_impl->meshes.insert(std::move(res));
    return h;
}

TexHandle GPUDevice::createTexture(const TexDesc& desc, const void* pixels) {
    TextureResource res;

    glGenTextures(1, &res.id);
    glBindTexture(GL_TEXTURE_2D, res.id);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, ToGLFilter(desc.minFilter));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, ToGLFilter(desc.magFilter));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, ToGLWrap(desc.wrapU));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, ToGLWrap(desc.wrapV));

    glTexImage2D(GL_TEXTURE_2D, 0,
                 ToGLInternalFormat(desc.format),
                 desc.width, desc.height, 0,
                 ToGLFormat(desc.format),
                 ToGLType(desc.format), pixels);

    if (desc.genMips) {
        glGenerateMipmap(GL_TEXTURE_2D);
    }

    res.width = desc.width;
    res.height = desc.height;
    res.format = desc.format;

    TexHandle h;
    h.value = m_impl->textures.insert(std::move(res));
    return h;
}

static GLuint CompileShader(GLenum type, const char* src) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    GLint success = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[512];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

static GLuint LinkProgram(GLuint vs, GLuint fs) {
    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glBindAttribLocation(program, 0, "a_position");
    glBindAttribLocation(program, 1, "a_normal");
    glBindAttribLocation(program, 2, "a_uv0");
    glBindAttribLocation(program, 3, "a_color");
    glBindAttribLocation(program, 4, "a_tangent");
    glLinkProgram(program);

    GLint success = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char log[512];
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        glDeleteProgram(program);
        return 0;
    }

    return program;
}

ShaderHandle GPUDevice::createShader(const ShaderDesc& desc) {
    ShaderResource res;

    int passIdx = 0;
    for (const auto& passDesc : desc.passes) {
        GLuint vs = CompileShader(GL_VERTEX_SHADER, passDesc.vs.data());
        GLuint fs = CompileShader(GL_FRAGMENT_SHADER, passDesc.fs.data());

        if (!vs || !fs) {
            if (vs) glDeleteShader(vs);
            if (fs) glDeleteShader(fs);
            continue;
        }

        GLuint program = LinkProgram(vs, fs);
        glDeleteShader(vs);
        glDeleteShader(fs);

        if (!program) continue;

        ShaderPass pass;
        pass.program = program;
        pass.state = passDesc.stateOverride.value_or(RenderState{});

        GLint numUniforms = 0;
        glGetProgramiv(program, GL_ACTIVE_UNIFORMS, &numUniforms);
        for (int i = 0; i < numUniforms; ++i) {
            char name[64];
            GLsizei len = 0;
            GLint size = 0;
            GLenum type = 0;
            glGetActiveUniform(program, i, sizeof(name), &len, &size, &type, name);
            GLint loc = glGetUniformLocation(program, name);
            if (loc >= 0) {
                pass.uniformLocs[name] = loc;
                int32_t nameHash = HashName(std::string_view(name));
                pass.uniformHashLocs[nameHash] = loc;
            }
        }

        res.passes.push_back(std::move(pass));
        res.passIndex[passDesc.name] = passIdx++;
    }

    ShaderHandle h;
    h.value = m_impl->shaders.insert(std::move(res));
    return h;
}

RTHandle GPUDevice::createRenderTarget(const RTDesc& desc) {
    RenderTargetResource res;
    res.width = desc.width;
    res.height = desc.height;

    glGenFramebuffers(1, &res.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, res.fbo);

    std::vector<GLenum> drawBuffers;

    for (int i = 0; i < static_cast<int>(desc.colorFormats.size()); ++i) {
        TextureResource texRes;
        glGenTextures(1, &texRes.id);
        glBindTexture(GL_TEXTURE_2D, texRes.id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glTexImage2D(GL_TEXTURE_2D, 0,
                     ToGLInternalFormat(desc.colorFormats[i]),
                     desc.width, desc.height, 0,
                     ToGLFormat(desc.colorFormats[i]),
                     ToGLType(desc.colorFormats[i]), nullptr);

        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i,
                               GL_TEXTURE_2D, texRes.id, 0);

        texRes.width = desc.width;
        texRes.height = desc.height;
        texRes.format = desc.colorFormats[i];

        TexHandle texH;
        texH.value = m_impl->textures.insert(std::move(texRes));
        res.colorTextures.push_back(texH);
        drawBuffers.push_back(GL_COLOR_ATTACHMENT0 + i);
    }

    if (desc.hasDepth) {
        glGenRenderbuffers(1, &res.depthRb);
        glBindRenderbuffer(GL_RENDERBUFFER, res.depthRb);
        glRenderbufferStorage(GL_RENDERBUFFER, ToGLInternalFormat(desc.depthFormat),
                              desc.width, desc.height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                  GL_RENDERBUFFER, res.depthRb);
    }

    if (!drawBuffers.empty()) {
        glDrawBuffers(static_cast<GLsizei>(drawBuffers.size()), drawBuffers.data());
    }

    glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    RTHandle h;
    h.value = m_impl->renderTargets.insert(std::move(res));
    return h;
}

void GPUDevice::destroyMesh(MeshHandle h) {
    MeshResource* res = m_impl->meshes.get(h.value);
    if (!res) return;
    if (res->vao) glDeleteVertexArrays(1, &res->vao);
    if (res->vbo) glDeleteBuffers(1, &res->vbo);
    if (res->ibo) glDeleteBuffers(1, &res->ibo);
    m_impl->meshes.remove(h.value);
}

void GPUDevice::destroyTexture(TexHandle h) {
    TextureResource* res = m_impl->textures.get(h.value);
    if (!res) return;
    if (res->id) glDeleteTextures(1, &res->id);
    m_impl->textures.remove(h.value);
}

void GPUDevice::destroyShader(ShaderHandle h) {
    ShaderResource* res = m_impl->shaders.get(h.value);
    if (!res) return;
    for (auto& pass : res->passes) {
        if (pass.program) glDeleteProgram(pass.program);
    }
    m_impl->shaders.remove(h.value);
}

void GPUDevice::destroyRenderTarget(RTHandle h) {
    RenderTargetResource* res = m_impl->renderTargets.get(h.value);
    if (!res) return;
    if (res->fbo) glDeleteFramebuffers(1, &res->fbo);
    if (res->depthRb) glDeleteRenderbuffers(1, &res->depthRb);
    for (TexHandle tex : res->colorTextures) {
        destroyTexture(tex);
    }
    m_impl->renderTargets.remove(h.value);
}

bool GPUDevice::isValid(MeshHandle h) const {
    return m_impl->meshes.isValid(h.value);
}

bool GPUDevice::isValid(TexHandle h) const {
    return m_impl->textures.isValid(h.value);
}

bool GPUDevice::isValid(ShaderHandle h) const {
    return m_impl->shaders.isValid(h.value);
}

bool GPUDevice::isValid(RTHandle h) const {
    return m_impl->renderTargets.isValid(h.value);
}

int GPUDevice::meshVertexCount(MeshHandle h) const {
    MeshResource* res = m_impl->meshes.get(h.value);
    return res ? res->vertexCount : 0;
}

int GPUDevice::meshIndexCount(MeshHandle h) const {
    MeshResource* res = m_impl->meshes.get(h.value);
    return res ? res->indexCount : 0;
}

bool GPUDevice::meshIsIndexed(MeshHandle h) const {
    MeshResource* res = m_impl->meshes.get(h.value);
    return res ? res->indexed : false;
}

int GPUDevice::rtWidth(RTHandle h) const {
    RenderTargetResource* res = m_impl->renderTargets.get(h.value);
    return res ? res->width : 0;
}

int GPUDevice::rtHeight(RTHandle h) const {
    RenderTargetResource* res = m_impl->renderTargets.get(h.value);
    return res ? res->height : 0;
}

TexHandle GPUDevice::rtColorTexture(RTHandle h, int index) const {
    RenderTargetResource* res = m_impl->renderTargets.get(h.value);
    if (!res || index < 0 || index >= static_cast<int>(res->colorTextures.size())) return TexHandle{};
    return res->colorTextures[index];
}

TexHandle GPUDevice::rtDepthTexture(RTHandle h) const {
    RenderTargetResource* res = m_impl->renderTargets.get(h.value);
    return res ? res->depthTexture : TexHandle{};
}

void GPUDevice::bindMesh(MeshHandle h) {
    if (!isValid(h)) {
        if (m_impl->boundVAO != 0) {
            glBindVertexArray(0);
            m_impl->boundVAO = 0;
        }
        return;
    }

    MeshResource* res = m_impl->meshes.get(h.value);
    if (!res) return;

    if (m_impl->boundVAO != res->vao) {
        glBindVertexArray(res->vao);
        m_impl->boundVAO = res->vao;
    }
}

void GPUDevice::bindTexture(TexHandle h, int slot) {
    if (slot < 0 || slot >= m_impl->caps.maxTexUnits) return;

    if (m_impl->activeTexSlot != slot) {
        glActiveTexture(GL_TEXTURE0 + slot);
        m_impl->activeTexSlot = slot;
    }

    GLuint id = 0;
    TextureResource* res = m_impl->textures.get(h.value);
    if (res) id = res->id;
    glBindTexture(GL_TEXTURE_2D, id);
}

void GPUDevice::bindRenderTarget(RTHandle h) {
    if (!isValid(h)) {
        bindDefaultFramebuffer();
        return;
    }

    RenderTargetResource* res = m_impl->renderTargets.get(h.value);
    if (!res) return;

    if (m_impl->boundFBO != res->fbo) {
        glBindFramebuffer(GL_FRAMEBUFFER, res->fbo);
        m_impl->boundFBO = res->fbo;
    }
}

void GPUDevice::bindDefaultFramebuffer() {
    if (m_impl->boundFBO != 0) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        m_impl->boundFBO = 0;
    }
}

void GPUDevice::useShader(ShaderHandle h, std::string_view passName) {
    if (!isValid(h)) {
        if (m_impl->boundProgram != 0) {
            glUseProgram(0);
            m_impl->boundProgram = 0;
        }
        return;
    }

    ShaderResource* res = m_impl->shaders.get(h.value);
    if (!res) return;

    int passIdx = 0;
    auto it = res->passIndex.find(std::string(passName));
    if (it != res->passIndex.end()) {
        passIdx = it->second;
    }

    if (passIdx >= 0 && passIdx < static_cast<int>(res->passes.size())) {
        GLuint program = res->passes[passIdx].program;
        if (m_impl->boundProgram != program) {
            glUseProgram(program);
            m_impl->boundProgram = program;
        }
    }
}

static GLint GetUniformLocation(ShaderResource* res, std::string_view name) {
    if (!res) return -1;
    std::string key(name);
    for (auto& pass : res->passes) {
        auto it = pass.uniformLocs.find(key);
        if (it != pass.uniformLocs.end()) {
            return it->second;
        }
    }
    return -1;
}

static GLint GetUniformLocationByHash(ShaderResource* res, int32_t nameHash) {
    if (!res) return -1;
    for (auto& pass : res->passes) {
        auto it = pass.uniformHashLocs.find(nameHash);
        if (it != pass.uniformHashLocs.end()) {
            return it->second;
        }
    }
    return -1;
}

void GPUDevice::setUniformInt(ShaderHandle h, std::string_view name, int val) {
    if (!isValid(h)) return;
    ShaderResource* res = m_impl->shaders.get(h.value);
    GLint loc = GetUniformLocation(res, name);
    if (loc >= 0) glUniform1i(loc, val);
}

void GPUDevice::setUniformFloat(ShaderHandle h, std::string_view name, float val) {
    if (!isValid(h)) return;
    ShaderResource* res = m_impl->shaders.get(h.value);
    GLint loc = GetUniformLocation(res, name);
    if (loc >= 0) glUniform1f(loc, val);
}

void GPUDevice::setUniformVec2(ShaderHandle h, std::string_view name, Vec2 val) {
    if (!isValid(h)) return;
    ShaderResource* res = m_impl->shaders.get(h.value);
    GLint loc = GetUniformLocation(res, name);
    if (loc >= 0) glUniform2f(loc, val.x, val.y);
}

void GPUDevice::setUniformVec3(ShaderHandle h, std::string_view name, Vec3 val) {
    if (!isValid(h)) return;
    ShaderResource* res = m_impl->shaders.get(h.value);
    GLint loc = GetUniformLocation(res, name);
    if (loc >= 0) glUniform3f(loc, val.x, val.y, val.z);
}

void GPUDevice::setUniformVec4(ShaderHandle h, std::string_view name, Vec4 val) {
    if (!isValid(h)) return;
    ShaderResource* res = m_impl->shaders.get(h.value);
    GLint loc = GetUniformLocation(res, name);
    if (loc >= 0) glUniform4f(loc, val.x, val.y, val.z, val.w);
}

void GPUDevice::setUniformMat3(ShaderHandle h, std::string_view name, const Mat3& val) {
    if (!isValid(h)) return;
    ShaderResource* res = m_impl->shaders.get(h.value);
    GLint loc = GetUniformLocation(res, name);
    if (loc >= 0) glUniformMatrix3fv(loc, 1, GL_FALSE, &val[0][0]);
}

void GPUDevice::setUniformMat4(ShaderHandle h, std::string_view name, const Mat4& val) {
    if (!isValid(h)) return;
    ShaderResource* res = m_impl->shaders.get(h.value);
    GLint loc = GetUniformLocation(res, name);
    if (loc >= 0) glUniformMatrix4fv(loc, 1, GL_FALSE, &val[0][0]);
}

void GPUDevice::applyMaterialUniforms(ShaderHandle shader, const MaterialUniforms& u) {
    if (!isValid(shader)) return;
    ShaderResource* res = m_impl->shaders.get(shader.value);
    if (!res) return;

    for (int i = 0; i < u.texCount; ++i) {
        bindTexture(u.textures[i].tex, i);
        GLint loc = GetUniformLocationByHash(res, u.textures[i].nameHash);
        if (loc >= 0) {
            glUniform1i(loc, i);
        }
    }

    for (int i = 0; i < u.floatCount; ++i) {
        GLint loc = GetUniformLocationByHash(res, u.floats[i].nameHash);
        if (loc >= 0) glUniform1f(loc, u.floats[i].value);
    }

    for (int i = 0; i < u.vec4Count; ++i) {
        GLint loc = GetUniformLocationByHash(res, u.vec4s[i].nameHash);
        if (loc >= 0) glUniform4f(loc, u.vec4s[i].value.x, u.vec4s[i].value.y, 
                                   u.vec4s[i].value.z, u.vec4s[i].value.w);
    }

    for (int i = 0; i < u.mat4Count; ++i) {
        GLint loc = GetUniformLocationByHash(res, u.mat4s[i].nameHash);
        if (loc >= 0) glUniformMatrix4fv(loc, 1, GL_FALSE, &u.mat4s[i].value[0][0]);
    }

    applyRenderState(u.state);
}

void GPUDevice::applyRenderState(const RenderState& state) {
    if (state.blend != m_impl->currentState.blend) {
        if (state.blend != BlendMode::Disabled) {
            glEnable(GL_BLEND);
            ApplyBlendMode(state.blend);
        } else {
            glDisable(GL_BLEND);
        }
        m_impl->currentState.blend = state.blend;
    }

    if (state.depth_test != m_impl->currentState.depth_test) {
        if (state.depth_test) {
            glEnable(GL_DEPTH_TEST);
        } else {
            glDisable(GL_DEPTH_TEST);
        }
        m_impl->currentState.depth_test = state.depth_test;
    }

    if (state.depth_test && state.depth_func != m_impl->currentState.depth_func) {
        glDepthFunc(ToGLDepthFunc(state.depth_func));
        m_impl->currentState.depth_func = state.depth_func;
    }

    if (state.depth_write != m_impl->currentState.depth_write) {
        glDepthMask(state.depth_write ? GL_TRUE : GL_FALSE);
        m_impl->currentState.depth_write = state.depth_write;
    }

    if (state.cull != m_impl->currentState.cull) {
        if (state.cull == CullFace::None) {
            glDisable(GL_CULL_FACE);
        } else {
            glEnable(GL_CULL_FACE);
            glCullFace(ToGLCullFace(state.cull));
        }
        m_impl->currentState.cull = state.cull;
    }
}

void GPUDevice::drawIndexed(int indexCount) {
    if (m_impl->boundVAO == 0) return;

    MeshResource* mesh = nullptr;
    m_impl->meshes.forEach([&](MeshResource& m) {
        if (m.vao == m_impl->boundVAO) mesh = &m;
    });
    if (!mesh || !mesh->indexed) return;

    GLenum idxType = mesh->use32bit ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT;
    glDrawElements(GL_TRIANGLES, indexCount > 0 ? indexCount : mesh->indexCount, idxType, nullptr);
}

void GPUDevice::drawArrays(int vertexCount) {
    if (m_impl->boundVAO == 0) return;

    MeshResource* mesh = nullptr;
    m_impl->meshes.forEach([&](MeshResource& m) {
        if (m.vao == m_impl->boundVAO) mesh = &m;
    });
    if (!mesh) return;

    glDrawArrays(GL_TRIANGLES, 0, vertexCount > 0 ? vertexCount : mesh->vertexCount);
}

void GPUDevice::drawFullscreenTriangle() {
    if (!isValid(m_impl->fullscreenTri)) return;

    bindMesh(m_impl->fullscreenTri);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

void GPUDevice::processResourceCommands(const GameWork& work) {
    const uint8_t* poolBase = work.uniformPool.data();

    for (const auto& cmd : work.resourceCommands) {
        const void* data = poolBase + cmd.dataOffset;

        switch (cmd.type) {
            case ResourceCmdType::CreateMesh: {
                const MeshData* meshData = reinterpret_cast<const MeshData*>(data);
                if (meshData) createMesh(*meshData);
                break;
            }
            case ResourceCmdType::CreateTexture: {
                const TexDesc* desc = reinterpret_cast<const TexDesc*>(data);
                if (desc) {
                    const void* pixels = poolBase + cmd.dataOffset + sizeof(TexDesc);
                    createTexture(*desc, pixels);
                }
                break;
            }
            case ResourceCmdType::CreateShader: {
                const ShaderDesc* desc = reinterpret_cast<const ShaderDesc*>(data);
                if (desc) createShader(*desc);
                break;
            }
            case ResourceCmdType::CreateRenderTarget: {
                const RTDesc* desc = reinterpret_cast<const RTDesc*>(data);
                if (desc) createRenderTarget(*desc);
                break;
            }
            case ResourceCmdType::DestroyMesh: {
                MeshHandle h; h.value = cmd.handle;
                destroyMesh(h);
                break;
            }
            case ResourceCmdType::DestroyTexture: {
                TexHandle h; h.value = cmd.handle;
                destroyTexture(h);
                break;
            }
            case ResourceCmdType::DestroyShader: {
                ShaderHandle h; h.value = cmd.handle;
                destroyShader(h);
                break;
            }
            case ResourceCmdType::DestroyRenderTarget: {
                RTHandle h; h.value = cmd.handle;
                destroyRenderTarget(h);
                break;
            }
            case ResourceCmdType::UpdateTexture:
                break;
        }
    }
}

TexHandle GPUDevice::whiteTex() const noexcept { return m_impl->whiteTex; }
TexHandle GPUDevice::blackTex() const noexcept { return m_impl->blackTex; }
TexHandle GPUDevice::normalTex() const noexcept { return m_impl->normalTex; }
MeshHandle GPUDevice::fullscreenTriMesh() const noexcept { return m_impl->fullscreenTri; }

} // namespace spt3d
