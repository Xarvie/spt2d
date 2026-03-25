#pragma once

#include "../Handle.h"
#include "GPUDevice.h"

#include <string>
#include <unordered_map>
#include <mutex>

namespace spt3d {

class ShaderLibrary {
public:
    static ShaderLibrary& Instance();

    void initialize(GPUDevice* gpu);
    void shutdown();

    ShaderHandle registerShader(const std::string& name, const ShaderDesc& desc);
    ShaderHandle getShader(const std::string& name) const;
    bool hasShader(const std::string& name) const;

    ShaderHandle unlit() const noexcept { return m_unlit; }
    ShaderHandle phong() const noexcept { return m_phong; }
    ShaderHandle blit() const noexcept { return m_blit; }
    ShaderHandle depth() const noexcept { return m_depth; }
    ShaderHandle skybox() const noexcept { return m_skybox; }

private:
    ShaderLibrary() = default;
    ~ShaderLibrary() = default;
    ShaderLibrary(const ShaderLibrary&) = delete;
    ShaderLibrary& operator=(const ShaderLibrary&) = delete;

    void registerBuiltinShaders();

    GPUDevice* m_gpu = nullptr;
    std::unordered_map<std::string, ShaderHandle> m_shaders;
    ShaderHandle m_unlit;
    ShaderHandle m_phong;
    ShaderHandle m_blit;
    ShaderHandle m_depth;
    ShaderHandle m_skybox;
};

}
