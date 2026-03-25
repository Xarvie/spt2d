#include "ShaderLibrary.h"
#include "BuiltinShaders.h"

#include <iostream>

namespace spt3d {

ShaderLibrary& ShaderLibrary::Instance() {
    static ShaderLibrary instance;
    return instance;
}

void ShaderLibrary::initialize(GPUDevice* gpu) {
    if (!gpu) {
        std::cerr << "[ShaderLibrary] GPUDevice is null" << std::endl;
        return;
    }
    m_gpu = gpu;
    registerBuiltinShaders();
    std::cout << "[ShaderLibrary] Initialized" << std::endl;
}

void ShaderLibrary::shutdown() {
    if (m_gpu) {
        if (m_unlit.value) m_gpu->destroyShader(m_unlit);
        if (m_phong.value) m_gpu->destroyShader(m_phong);
        if (m_blit.value) m_gpu->destroyShader(m_blit);
        if (m_depth.value) m_gpu->destroyShader(m_depth);
        if (m_skybox.value) m_gpu->destroyShader(m_skybox);
        
        for (auto& pair : m_shaders) {
            if (pair.second.value) {
                m_gpu->destroyShader(pair.second);
            }
        }
    }
    m_shaders.clear();
    m_gpu = nullptr;
    std::cout << "[ShaderLibrary] Shutdown" << std::endl;
}

ShaderHandle ShaderLibrary::registerShader(const std::string& name, const ShaderDesc& desc) {
    auto it = m_shaders.find(name);
    if (it != m_shaders.end()) {
        return it->second;
    }

    ShaderHandle handle = m_gpu->createShader(desc);
    if (handle.value == 0) {
        std::cerr << "[ShaderLibrary] Failed to create shader: " << name << std::endl;
        return ShaderHandle{};
    }

    m_shaders[name] = handle;
    return handle;
}

ShaderHandle ShaderLibrary::getShader(const std::string& name) const {
    auto it = m_shaders.find(name);
    if (it != m_shaders.end()) {
        return it->second;
    }
    return ShaderHandle{};
}

bool ShaderLibrary::hasShader(const std::string& name) const {
    return m_shaders.find(name) != m_shaders.end();
}

void ShaderLibrary::registerBuiltinShaders() {
    {
        ShaderDesc desc;
        ShaderPassDesc pass;
        pass.name = "FORWARD";
        pass.vs = shaders::kUnlitVS;
        pass.fs = shaders::kUnlitFS;
        desc.passes.push_back(pass);
        m_unlit = m_gpu->createShader(desc);
        m_shaders["unlit"] = m_unlit;
    }

    {
        ShaderDesc desc;
        ShaderPassDesc pass;
        pass.name = "FORWARD";
        pass.vs = shaders::kPhongVS;
        pass.fs = shaders::kPhongFS;
        desc.passes.push_back(pass);
        m_phong = m_gpu->createShader(desc);
        m_shaders["phong"] = m_phong;
    }

    {
        ShaderDesc desc;
        ShaderPassDesc pass;
        pass.name = "BLIT";
        pass.vs = shaders::kBlitVS;
        pass.fs = shaders::kBlitFS;
        desc.passes.push_back(pass);
        m_blit = m_gpu->createShader(desc);
        m_shaders["blit"] = m_blit;
    }

    {
        ShaderDesc desc;
        ShaderPassDesc pass;
        pass.name = "DEPTH";
        pass.vs = shaders::kDepthVS;
        pass.fs = shaders::kDepthFS;
        desc.passes.push_back(pass);
        m_depth = m_gpu->createShader(desc);
        m_shaders["depth"] = m_depth;
    }

    {
        ShaderDesc desc;
        ShaderPassDesc pass;
        pass.name = "SKYBOX";
        pass.vs = shaders::kSkyboxVS;
        pass.fs = shaders::kSkyboxFS;
        desc.passes.push_back(pass);
        m_skybox = m_gpu->createShader(desc);
        m_shaders["skybox"] = m_skybox;
    }

    std::cout << "[ShaderLibrary] Registered 5 builtin shaders" << std::endl;
}

}
