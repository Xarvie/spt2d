#pragma once

#include "../glad/glad.h"
#include <cstdint>
#include <vector>

namespace spt3d {

class Texture {
public:
    Texture() = default;
    ~Texture() { cleanup(); }

    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    Texture(Texture&& other) noexcept;
    Texture& operator=(Texture&& other) noexcept;

    bool loadFromMemory(const uint8_t* data, size_t size);
    bool loadFromMemory(const std::vector<uint8_t>& data);
    bool uploadToGPU();
    void cleanup();

    [[nodiscard]] bool isReady() const { return m_glTexture != 0 && m_width > 0; }
    [[nodiscard]] GLuint getID() const { return m_glTexture; }
    [[nodiscard]] int getWidth() const { return m_width; }
    [[nodiscard]] int getHeight() const { return m_height; }
    [[nodiscard]] int getChannels() const { return m_channels; }

private:
    std::vector<uint8_t> m_pixelData;
    GLuint m_glTexture = 0;
    int m_width = 0;
    int m_height = 0;
    int m_channels = 0;
};

}
