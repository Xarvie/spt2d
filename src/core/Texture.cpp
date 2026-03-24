#include "../Spt3D.h"
#include "../glad/glad.h"
#include "../vfs/VirtualFileSystem.h"

#include "../libs/stb/stb_image.h"

#include <unordered_map>
#include <mutex>
#include <random>

namespace spt3d {

struct Texture::Impl {
    GLuint glTexture = 0;
    int width = 0;
    int height = 0;
    Fmt format = Fmt::RGBA8;
    bool valid = false;
    bool ownsGL = true;
    
    ~Impl() {
        if (ownsGL && glTexture != 0) {
            glDeleteTextures(1, &glTexture);
        }
    }
};

bool Texture::Valid() const { return p && p->valid; }
int Texture::W() const { return p ? p->width : 0; }
int Texture::H() const { return p ? p->height : 0; }
Fmt Texture::Format() const { return p ? p->format : Fmt::RGBA8; }
uint32_t Texture::GL() const { return p ? p->glTexture : 0; }
bool Texture::Ready() const { return Valid(); }
ResState Texture::State() const { return Valid() ? ResState::Ready : ResState::Failed; }

namespace detail {

Texture CreateTexFromGL(uint32_t glTex, int w, int h, Fmt fmt) {
    Texture tex;
    tex.p = std::make_shared<Texture::Impl>();
    tex.p->glTexture = glTex;
    tex.p->width = w;
    tex.p->height = h;
    tex.p->format = fmt;
    tex.p->valid = true;
    tex.p->ownsGL = false;
    return tex;
}

}

static std::unordered_map<uint32_t, std::weak_ptr<Texture::Impl>> g_textureCache;
static std::mutex g_textureMutex;
static uint32_t g_nextTextureId = 1;

Texture CreateTex(int w, int h, Fmt fmt, const void* px) {
    Texture tex;
    tex.p = std::make_shared<Texture::Impl>();
    tex.p->width = w;
    tex.p->height = h;
    tex.p->format = fmt;
    
    GLuint glFmt = GL_RGBA;
    switch (fmt) {
        case Fmt::R8: glFmt = GL_R8; break;
        case Fmt::RG8: glFmt = GL_RG8; break;
        case Fmt::RGBA8: glFmt = GL_RGBA8; break;
        case Fmt::RGBA16F: glFmt = GL_RGBA16F; break;
        case Fmt::RGBA32F: glFmt = GL_RGBA32F; break;
        case Fmt::D24S8: glFmt = GL_DEPTH24_STENCIL8; break;
        case Fmt::D32F: glFmt = GL_DEPTH32F_STENCIL8; break;
        default: break;
    }
    
    GLuint glDataFmt = GL_RGBA;
    GLuint glDataType = GL_UNSIGNED_BYTE;
    if (fmt == Fmt::RGBA16F) { glDataFmt = GL_RGBA; glDataType = GL_HALF_FLOAT; }
    else if (fmt == Fmt::RGBA32F) { glDataFmt = GL_RGBA; glDataType = GL_FLOAT; }
    else if (fmt == Fmt::D24S8 || fmt == Fmt::D32F) { glDataFmt = GL_DEPTH_STENCIL; glDataType = GL_UNSIGNED_INT_24_8; }
    
    GLuint texId = 0;
    glGenTextures(1, &texId);
    glBindTexture(GL_TEXTURE_2D, texId);
    glTexImage2D(GL_TEXTURE_2D, 0, glFmt, w, h, 0, glDataFmt, glDataType, px);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    
    tex.p->glTexture = texId;
    tex.p->valid = true;
    
    return tex;
}

Texture LoadTex(std::string_view url, Callback cb) {
    auto tex = CreateTex(1, 1);
    auto impl = tex.p;
    
    spt3d::VirtualFileSystem::Instance().read(std::string(url), 
        [impl, cb](const uint8_t* data, size_t size, bool success) {
            if (success && data && impl) {
                int w, h, channels;
                uint8_t* pixels = stbi_load_from_memory(data, static_cast<int>(size), &w, &h, &channels, 4);
                if (pixels) {
                    GLuint texId = impl->glTexture;
                    if (texId) {
                        glBindTexture(GL_TEXTURE_2D, texId);
                        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
                        glBindTexture(GL_TEXTURE_2D, 0);
                        impl->width = w;
                        impl->height = h;
                    }
                    stbi_image_free(pixels);
                    if (cb) cb(true);
                    return;
                }
            }
            if (cb) cb(false);
        });
    
    return tex;
}

Texture LoadTexMem(const uint8_t* data, size_t sz, std::string_view hint, Callback cb) {
    (void)hint;
    
    int w, h, channels;
    uint8_t* pixels = stbi_load_from_memory(data, static_cast<int>(sz), &w, &h, &channels, 4);
    if (!pixels) {
        if (cb) cb(false);
        return CreateTex(1, 1);
    }
    
    auto tex = CreateTex(w, h, Fmt::RGBA8, pixels);
    stbi_image_free(pixels);
    
    if (cb) cb(true);
    return tex;
}

Texture SolidTex(Color c) {
    uint8_t px[4] = { c.r, c.g, c.b, c.a };
    return CreateTex(1, 1, Fmt::RGBA8, px);
}

Texture WhiteTex() { return SolidTex(Colors::White); }
Texture BlackTex() { return SolidTex(Colors::Black); }

Texture NormalTex() {
    uint8_t px[4] = { 128, 128, 255, 255 };
    return CreateTex(1, 1, Fmt::RGBA8, px);
}

Texture NoiseTex(int w, int h) {
    std::vector<uint8_t> data(w * h * 4);
    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(0, 255);
    for (int i = 0; i < w * h * 4; i += 4) {
        uint8_t v = static_cast<uint8_t>(dist(rng));
        data[i] = data[i+1] = data[i+2] = v;
        data[i+3] = 255;
    }
    return CreateTex(w, h, Fmt::RGBA8, data.data());
}

void SetFilter(Texture t, Filter min_f, Filter mag_f) {
    if (!t.Valid()) return;
    GLuint texId = t.GL();
    glBindTexture(GL_TEXTURE_2D, texId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, 
        min_f == Filter::Nearest ? GL_NEAREST : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, 
        mag_f == Filter::Nearest ? GL_NEAREST : GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void SetWrap(Texture t, Wrap u, Wrap v) {
    if (!t.Valid()) return;
    GLuint texId = t.GL();
    glBindTexture(GL_TEXTURE_2D, texId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, 
        u == Wrap::Clamp ? GL_CLAMP_TO_EDGE : GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, 
        v == Wrap::Clamp ? GL_CLAMP_TO_EDGE : GL_REPEAT);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void GenMips(Texture t) {
    if (!t.Valid()) return;
    GLuint texId = t.GL();
    glBindTexture(GL_TEXTURE_2D, texId);
    glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Update(Texture t, const void* px) {
    if (!t.Valid() || !px) return;
    GLuint texId = t.GL();
    glBindTexture(GL_TEXTURE_2D, texId);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, t.W(), t.H(), GL_RGBA, GL_UNSIGNED_BYTE, px);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void UpdateRegion(Texture t, Rect region, const void* px) {
    if (!t.Valid() || !px) return;
    GLuint texId = t.GL();
    glBindTexture(GL_TEXTURE_2D, texId);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 
        static_cast<GLint>(region.x), static_cast<GLint>(region.y),
        static_cast<GLint>(region.w), static_cast<GLint>(region.h),
        GL_RGBA, GL_UNSIGNED_BYTE, px);
    glBindTexture(GL_TEXTURE_2D, 0);
}

int FmtBytesPerPixel(Fmt f) {
    switch (f) {
        case Fmt::R8: return 1;
        case Fmt::RG8: return 2;
        case Fmt::RGBA8: return 4;
        case Fmt::RGB8: return 3;
        case Fmt::SRGB8: return 3;
        case Fmt::SRGB8A8: return 4;
        case Fmt::R16F: return 2;
        case Fmt::RG16F: return 4;
        case Fmt::RGBA16F: return 8;
        case Fmt::R32F: return 4;
        case Fmt::RG32F: return 8;
        case Fmt::RGBA32F: return 16;
        case Fmt::R11G11B10F: return 4;
        case Fmt::Depth16: return 2;
        case Fmt::Depth24: return 3;
        case Fmt::D24S8: return 4;
        case Fmt::D32F: return 4;
        default: return 4;
    }
}

bool FmtIsDepth(Fmt f) {
    return f == Fmt::Depth16 || f == Fmt::Depth24 || f == Fmt::D24S8 || f == Fmt::D32F;
}

bool FmtIsCompressed(Fmt f) {
    return f == Fmt::ETC2_RGB || f == Fmt::ETC2_RGBA || f == Fmt::ASTC_4x4 || f == Fmt::ASTC_8x8;
}

bool FmtIsFloat(Fmt f) {
    switch (f) {
        case Fmt::R16F:
        case Fmt::RG16F:
        case Fmt::RGBA16F:
        case Fmt::R32F:
        case Fmt::RG32F:
        case Fmt::RGBA32F:
        case Fmt::R11G11B10F:
        case Fmt::D32F:
            return true;
        default:
            return false;
    }
}

}
