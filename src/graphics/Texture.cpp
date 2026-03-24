#include "Texture.h"

#define STB_IMAGE_IMPLEMENTATION
#include "../../libs/stb/stb_image.h"

namespace spt {

Texture::Texture(Texture&& other) noexcept
    : m_pixelData(std::move(other.m_pixelData))
    , m_glTexture(other.m_glTexture)
    , m_width(other.m_width)
    , m_height(other.m_height)
    , m_channels(other.m_channels)
{
    other.m_glTexture = 0;
    other.m_width = 0;
    other.m_height = 0;
    other.m_channels = 0;
}

Texture& Texture::operator=(Texture&& other) noexcept {
    if (this != &other) {
        cleanup();
        m_pixelData = std::move(other.m_pixelData);
        m_glTexture = other.m_glTexture;
        m_width = other.m_width;
        m_height = other.m_height;
        m_channels = other.m_channels;
        other.m_glTexture = 0;
        other.m_width = 0;
        other.m_height = 0;
        other.m_channels = 0;
    }
    return *this;
}

bool Texture::loadFromMemory(const uint8_t* data, size_t size) {
    if (!data || size == 0) return false;

    int w, h, c;
    uint8_t* pixels = stbi_load_from_memory(data, static_cast<int>(size), &w, &h, &c, 4);
    if (!pixels) {
        return false;
    }

    m_width = w;
    m_height = h;
    m_channels = 4;

    m_pixelData.resize(static_cast<size_t>(w * h * 4));
    std::memcpy(m_pixelData.data(), pixels, m_pixelData.size());

    stbi_image_free(pixels);
    return true;
}

bool Texture::loadFromMemory(const std::vector<uint8_t>& data) {
    return loadFromMemory(data.data(), data.size());
}

bool Texture::uploadToGPU() {
    if (m_pixelData.empty() || m_width <= 0 || m_height <= 0) {
        return false;
    }

    if (m_glTexture == 0) {
        glGenTextures(1, &m_glTexture);
    }

    glBindTexture(GL_TEXTURE_2D, m_glTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_width, m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, m_pixelData.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    m_pixelData.clear();
    m_pixelData.shrink_to_fit();

    return true;
}

void Texture::cleanup() {
    if (m_glTexture != 0) {
        glDeleteTextures(1, &m_glTexture);
        m_glTexture = 0;
    }
    m_pixelData.clear();
    m_width = 0;
    m_height = 0;
    m_channels = 0;
}

}
