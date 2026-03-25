#pragma once

#include "../resource/IResource.h"
#include "../graphics/Texture.h"
#include <atomic>
#include <mutex>
#include <memory>

namespace spt3d {

class TextureResource : public IResource {
public:
    static constexpr ResType StaticType = ResType::Texture;

    TextureResource(const std::string& logicAddress, const std::string& physicalPath);
    ~TextureResource() override;

    ResType getType() const override { return ResType::Texture; }
    ResState getState() const override { return m_state.load(std::memory_order_relaxed); }
    void setState(ResState state) override { m_state.store(state, std::memory_order_relaxed); }

    const std::string& getLogicAddress() const override { return m_logicAddress; }
    const std::string& getPhysicalPath() const override { return m_physicalPath; }

    size_t getMemoryUsage() const override;

    void cleanupGPU() override;
    void cleanupCPU() override;

    struct Config {
        GLenum minFilter = GL_LINEAR;
        GLenum magFilter = GL_LINEAR;
        GLenum wrapS = GL_CLAMP_TO_EDGE;
        GLenum wrapT = GL_CLAMP_TO_EDGE;
    };

    TextureResource* setFilter(GLenum min, GLenum mag);
    TextureResource* setWrap(GLenum s, GLenum t);

    bool loadFromMemory(const uint8_t* data, size_t size);
    bool loadFromMemory(const std::vector<uint8_t>& data);
    void uploadToGPU();

    gfx::Texture* getTexture() const { return m_texture.get(); }
    GLuint getGLTexture() const { return m_texture ? m_texture->getID() : 0; }
    int getWidth() const { return m_texture ? m_texture->getWidth() : 0; }
    int getHeight() const { return m_texture ? m_texture->getHeight() : 0; }

private:
    void applyConfigToGPU(const Config& cfg);

    std::string m_logicAddress;
    std::string m_physicalPath;
    std::atomic<ResState> m_state{ResState::Unloaded};
    std::unique_ptr<gfx::Texture> m_texture;

    Config m_config;
    std::mutex m_configMutex;
};

}
