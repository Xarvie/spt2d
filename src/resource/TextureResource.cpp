#include "TextureResource.h"
#include <iostream>

namespace spt3d {

TextureResource::TextureResource(const std::string& logicAddress, const std::string& physicalPath)
    : m_logicAddress(logicAddress)
    , m_physicalPath(physicalPath)
{
}

TextureResource::~TextureResource() {
    m_texture.reset();
}

size_t TextureResource::getMemoryUsage() const {
    if (!m_texture) return 0;
    return static_cast<size_t>(m_texture->getWidth()) * m_texture->getHeight() * 4;
}

void TextureResource::cleanupGPU() {
    if (m_texture) {
        m_texture->cleanup();
    }
    m_state.store(ResState::Unloaded, std::memory_order_relaxed);
}

void TextureResource::cleanupCPU() {
    m_texture.reset();
    m_state.store(ResState::Unloaded, std::memory_order_relaxed);
}

TextureResource* TextureResource::setFilter(GLenum min, GLenum mag) {
    {
        std::lock_guard<std::mutex> lock(m_configMutex);
        m_config.minFilter = min;
        m_config.magFilter = mag;
    }

    if (getState() == ResState::Ready) {
        dispatchGpuTask([this, min, mag]() {
            if (m_texture && m_texture->isReady()) {
                GLuint id = m_texture->getID();
                glBindTexture(GL_TEXTURE_2D, id);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, min);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, mag);
                glBindTexture(GL_TEXTURE_2D, 0);
            }
        });
    }
    return this;
}

TextureResource* TextureResource::setWrap(GLenum s, GLenum t) {
    {
        std::lock_guard<std::mutex> lock(m_configMutex);
        m_config.wrapS = s;
        m_config.wrapT = t;
    }

    if (getState() == ResState::Ready) {
        dispatchGpuTask([this, s, t]() {
            if (m_texture && m_texture->isReady()) {
                GLuint id = m_texture->getID();
                glBindTexture(GL_TEXTURE_2D, id);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, s);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, t);
                glBindTexture(GL_TEXTURE_2D, 0);
            }
        });
    }
    return this;
}

bool TextureResource::loadFromMemory(const uint8_t* data, size_t size) {
    if (!data || size == 0) {
        m_state.store(ResState::Failed, std::memory_order_relaxed);
        return false;
    }

    m_texture = std::make_unique<Texture>();
    if (!m_texture->loadFromMemory(data, size)) {
        m_state.store(ResState::Failed, std::memory_order_relaxed);
        return false;
    }

    m_state.store(ResState::Loaded, std::memory_order_relaxed);
    return true;
}

bool TextureResource::loadFromMemory(const std::vector<uint8_t>& data) {
    return loadFromMemory(data.data(), data.size());
}

void TextureResource::uploadToGPU() {
    if (!m_texture || getState() != ResState::Loaded) return;

    m_texture->uploadToGPU();

    if (m_texture->isReady()) {
        Config cfgSnapshot;
        {
            std::lock_guard<std::mutex> lock(m_configMutex);
            cfgSnapshot = m_config;
        }

        applyConfigToGPU(cfgSnapshot);
        m_state.store(ResState::Ready, std::memory_order_relaxed);
    } else {
        m_state.store(ResState::Failed, std::memory_order_relaxed);
    }
}

void TextureResource::applyConfigToGPU(const Config& cfg) {
    if (!m_texture) return;
    GLuint id = m_texture->getID();
    if (id == 0) return;

    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, cfg.minFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, cfg.magFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, cfg.wrapS);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, cfg.wrapT);
    glBindTexture(GL_TEXTURE_2D, 0);
}

}
