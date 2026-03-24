#include "../Spt3D.h"
#include "../glad/glad.h"

#include <vector>

namespace spt3d {

struct RenderTarget::Impl {
    GLuint fbo = 0;
    std::vector<std::shared_ptr<Texture::Impl>> colorTextures;
    std::shared_ptr<Texture::Impl> depthTexture;
    int width = 0;
    int height = 0;
    bool isScreen = false;
    
    Impl() = default;
    ~Impl() {
        if (fbo != 0 && !isScreen) {
            glDeleteFramebuffers(1, &fbo);
        }
    }
};

bool RenderTarget::Valid() const { return p && (p->isScreen || p->fbo != 0); }
int RenderTarget::W() const { return p ? p->width : 0; }
int RenderTarget::H() const { return p ? p->height : 0; }
int RenderTarget::ColorCount() const { return p ? static_cast<int>(p->colorTextures.size()) : 0; }
uint32_t RenderTarget::GL() const { return p ? p->fbo : 0; }
RenderTarget::operator bool() const { return Valid(); }

Texture RenderTarget::GetColor(int index) const {
    Texture tex;
    if (!p || index < 0 || index >= static_cast<int>(p->colorTextures.size())) {
        return tex;
    }
    tex.p = p->colorTextures[index];
    return tex;
}

Texture RenderTarget::GetDepth() const {
    Texture tex;
    if (p && p->depthTexture) {
        tex.p = p->depthTexture;
    }
    return tex;
}

RenderTarget ScreenTarget() {
    RenderTarget rt;
    rt.p = std::make_shared<RenderTarget::Impl>();
    rt.p->isScreen = true;
    rt.p->width = 0;
    rt.p->height = 0;
    return rt;
}

struct RTBuilder::Impl {
    int width = 0;
    int height = 0;
    float scale = 1.0f;
    std::vector<Fmt> colorFmts;
    std::vector<Filter> colorFilters;
    Fmt depthFmt = Fmt::D24S8;
    bool hasDepth = true;
};

RTBuilder::RTBuilder(int w, int h) : p(std::make_unique<Impl>()) {
    p->width = w;
    p->height = h;
}

RTBuilder& RTBuilder::AddColor(Fmt fmt) {
    p->colorFmts.push_back(fmt);
    p->colorFilters.push_back(Filter::Linear);
    return *this;
}

RTBuilder& RTBuilder::AddColorFiltered(Fmt fmt, Filter f) {
    p->colorFmts.push_back(fmt);
    p->colorFilters.push_back(f);
    return *this;
}

RTBuilder& RTBuilder::SetDepth(Fmt fmt) {
    p->depthFmt = fmt;
    p->hasDepth = true;
    return *this;
}

RTBuilder& RTBuilder::NoDepth() {
    p->hasDepth = false;
    return *this;
}

RTBuilder& RTBuilder::Scale(float s) {
    p->scale = s;
    return *this;
}

static GLuint getGLInternalFormat(Fmt fmt) {
    switch (fmt) {
        case Fmt::R8: return GL_R8;
        case Fmt::RG8: return GL_RG8;
        case Fmt::RGB8: return GL_RGB8;
        case Fmt::RGBA8: return GL_RGBA8;
        case Fmt::SRGB8: return GL_SRGB8;
        case Fmt::SRGB8A8: return GL_SRGB8_ALPHA8;
        case Fmt::R16F: return GL_R16F;
        case Fmt::RG16F: return GL_RG16F;
        case Fmt::RGBA16F: return GL_RGBA16F;
        case Fmt::R32F: return GL_R32F;
        case Fmt::RG32F: return GL_RG32F;
        case Fmt::RGBA32F: return GL_RGBA32F;
        case Fmt::R11G11B10F: return GL_R11F_G11F_B10F;
        case Fmt::Depth16: return GL_DEPTH_COMPONENT16;
        case Fmt::Depth24: return GL_DEPTH_COMPONENT24;
        case Fmt::D24S8: return GL_DEPTH24_STENCIL8;
        case Fmt::D32F: return GL_DEPTH32F_STENCIL8;
        default: return GL_RGBA8;
    }
}

static GLuint getGLFormat(Fmt fmt) {
    switch (fmt) {
        case Fmt::R8:
        case Fmt::R16F:
        case Fmt::R32F:
            return GL_RED;
        case Fmt::RG8:
        case Fmt::RG16F:
        case Fmt::RG32F:
            return GL_RG;
        case Fmt::RGB8:
        case Fmt::SRGB8:
        case Fmt::R11G11B10F:
            return GL_RGB;
        case Fmt::Depth16:
        case Fmt::Depth24:
            return GL_DEPTH_COMPONENT;
        case Fmt::D24S8:
        case Fmt::D32F:
            return GL_DEPTH_STENCIL;
        default:
            return GL_RGBA;
    }
}

static GLuint getGLType(Fmt fmt) {
    switch (fmt) {
        case Fmt::R16F:
        case Fmt::RG16F:
        case Fmt::RGBA16F:
            return GL_HALF_FLOAT;
        case Fmt::R32F:
        case Fmt::RG32F:
        case Fmt::RGBA32F:
        case Fmt::R11G11B10F:
            return GL_FLOAT;
        case Fmt::D24S8:
            return GL_UNSIGNED_INT_24_8;
        case Fmt::D32F:
            return GL_FLOAT_32_UNSIGNED_INT_24_8_REV;
        case Fmt::Depth16:
            return GL_UNSIGNED_SHORT;
        case Fmt::Depth24:
            return GL_UNSIGNED_INT;
        default:
            return GL_UNSIGNED_BYTE;
    }
}

static GLuint getGLAttachment(Fmt fmt) {
    switch (fmt) {
        case Fmt::Depth16:
        case Fmt::Depth24:
            return GL_DEPTH_ATTACHMENT;
        case Fmt::D24S8:
        case Fmt::D32F:
            return GL_DEPTH_STENCIL_ATTACHMENT;
        default:
            return GL_COLOR_ATTACHMENT0;
    }
}

RenderTarget RTBuilder::Build() {
    RenderTarget rt;
    rt.p = std::make_shared<RenderTarget::Impl>();
    
    int w = static_cast<int>(p->width * p->scale);
    int h = static_cast<int>(p->height * p->scale);
    rt.p->width = w;
    rt.p->height = h;
    
    if (p->colorFmts.empty()) {
        p->colorFmts.push_back(Fmt::RGBA8);
        p->colorFilters.push_back(Filter::Linear);
    }
    
    glGenFramebuffers(1, &rt.p->fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, rt.p->fbo);
    
    std::vector<GLenum> drawBuffers;
    
    for (size_t i = 0; i < p->colorFmts.size(); ++i) {
        GLuint tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        
        glTexImage2D(GL_TEXTURE_2D, 0, getGLInternalFormat(p->colorFmts[i]), w, h, 0,
                     getGLFormat(p->colorFmts[i]), getGLType(p->colorFmts[i]), nullptr);
        
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, 
            p->colorFilters[i] == Filter::Nearest ? GL_NEAREST : GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, 
            p->colorFilters[i] == Filter::Nearest ? GL_NEAREST : GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + static_cast<GLenum>(i), 
                               GL_TEXTURE_2D, tex, 0);
        
        Texture colorTex = detail::CreateTexFromGL(tex, w, h, p->colorFmts[i]);
        rt.p->colorTextures.push_back(colorTex.p);
        
        drawBuffers.push_back(GL_COLOR_ATTACHMENT0 + static_cast<GLenum>(i));
    }
    
    if (p->hasDepth) {
        GLuint depthTex = 0;
        glGenTextures(1, &depthTex);
        glBindTexture(GL_TEXTURE_2D, depthTex);
        
        glTexImage2D(GL_TEXTURE_2D, 0, getGLInternalFormat(p->depthFmt), w, h, 0,
                     getGLFormat(p->depthFmt), getGLType(p->depthFmt), nullptr);
        
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        
        glFramebufferTexture2D(GL_FRAMEBUFFER, getGLAttachment(p->depthFmt), 
                               GL_TEXTURE_2D, depthTex, 0);
        
        Texture depthTexObj = detail::CreateTexFromGL(depthTex, w, h, p->depthFmt);
        rt.p->depthTexture = depthTexObj.p;
    }
    
    if (drawBuffers.size() > 1) {
        glDrawBuffers(static_cast<GLsizei>(drawBuffers.size()), drawBuffers.data());
    }
    
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        rt.p.reset();
        return rt;
    }
    
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return rt;
}

RenderTarget CreateRT(int w, int h) {
    return RTBuilder(w, h).AddColor(Fmt::RGBA8).SetDepth(Fmt::D24S8).Build();
}

RenderTarget CreateRTHDR(int w, int h) {
    return RTBuilder(w, h).AddColor(Fmt::RGBA16F).SetDepth(Fmt::D24S8).Build();
}

RenderTarget CreateRTMRT(int w, int h, std::initializer_list<Fmt> fmts) {
    RTBuilder builder(w, h);
    for (auto fmt : fmts) {
        builder.AddColor(fmt);
    }
    return builder.SetDepth(Fmt::D24S8).Build();
}

}
