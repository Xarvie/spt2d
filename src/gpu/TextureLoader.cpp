#include "TextureLoader.h"
#include "../vfs/VirtualFileSystem.h"

#define STB_IMAGE_IMPLEMENTATION
#include "../../libs/stb/stb_image.h"

#include <iostream>

namespace spt3d {

TextureLoader& TextureLoader::Instance() {
    static TextureLoader instance;
    return instance;
}

void TextureLoader::initialize(GPUDevice* gpu) {
    if (!gpu) {
        std::cerr << "[TextureLoader] GPUDevice is null" << std::endl;
        return;
    }
    m_gpu = gpu;
    std::cout << "[TextureLoader] Initialized" << std::endl;
}

void TextureLoader::shutdown() {
    std::lock_guard<std::mutex> lock(m_mutex);
    while (!m_pendingTasks.empty()) {
        m_pendingTasks.pop();
    }
    while (!m_completedTasks.empty()) {
        m_completedTasks.pop();
    }
    m_gpu = nullptr;
    std::cout << "[TextureLoader] Shutdown" << std::endl;
}

void TextureLoader::loadAsync(const std::string& path, 
                               LoadCallback callback,
                               bool genMips) {
    std::cout << "[TextureLoader] loadAsync: " << path << std::endl;
    VirtualFileSystem::Instance().read(path, 
        [this, path, genMips, callback](const uint8_t* data, size_t size, bool success) {
            std::cout << "[TextureLoader] read callback: path=" << path 
                      << ", success=" << success << ", size=" << size << std::endl;
            if (!success || !data || size == 0) {
                TextureLoadResult result;
                result.path = path;
                result.success = false;
                result.error = "Failed to load file: " + path;
                
                std::cerr << "[TextureLoader] ERROR: " << result.error << std::endl;
                
                std::lock_guard<std::mutex> lock(m_mutex);
                CompletedTask completed;
                completed.result = result;
                completed.callback = callback;
                m_completedTasks.push(std::move(completed));
                return;
            }

            TextureLoadResult loadResult = loadFromMemory(data, size, genMips);
            loadResult.path = path;
            
            std::cout << "[TextureLoader] loadFromMemory: success=" << loadResult.success 
                      << ", handle=" << loadResult.handle.value << std::endl;

            std::lock_guard<std::mutex> lock(m_mutex);
            CompletedTask completed;
            completed.result = loadResult;
            completed.callback = callback;
            m_completedTasks.push(std::move(completed));
        });
}

TextureLoadResult TextureLoader::loadFromMemory(const uint8_t* data, size_t size, 
                                                  bool genMips) {
    TextureLoadResult result;
    result.success = false;

    if (!data || size == 0) {
        result.error = "Invalid data";
        return result;
    }

    int width, height, channels;
    stbi_set_flip_vertically_on_load(1);
    uint8_t* pixels = stbi_load_from_memory(data, static_cast<int>(size), 
                                             &width, &height, &channels, 4);
    
    if (!pixels) {
        result.error = "stb_image failed to decode";
        return result;
    }

    result = loadFromPixels(pixels, width, height, 4, genMips);
    stbi_image_free(pixels);
    
    return result;
}

TextureLoadResult TextureLoader::loadFromPixels(const uint8_t* pixels, 
                                                  int width, int height,
                                                  int channels, bool genMips) {
    TextureLoadResult result;
    result.success = false;

    if (!pixels || width <= 0 || height <= 0) {
        result.error = "Invalid pixel data";
        return result;
    }

    if (!m_gpu) {
        result.error = "GPUDevice not initialized";
        return result;
    }

    TexDesc desc;
    desc.width = width;
    desc.height = height;
    desc.format = (channels == 4) ? Fmt::RGBA8 : Fmt::RGB8;
    desc.genMips = genMips;
    desc.minFilter = genMips ? Filter::MipLinear : Filter::Linear;
    desc.magFilter = Filter::Linear;

    result.handle = m_gpu->createTexture(desc, pixels);
    result.success = result.handle.value != 0;

    if (!result.success) {
        result.error = "Failed to create GPU texture";
    }

    return result;
}

void TextureLoader::update() {
    std::queue<CompletedTask> tasksToProcess;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        tasksToProcess = std::move(m_completedTasks);
        m_completedTasks = std::queue<CompletedTask>();
    }

    while (!tasksToProcess.empty()) {
        CompletedTask& task = tasksToProcess.front();
        if (task.callback) {
            task.callback(task.result);
        }
        tasksToProcess.pop();
    }
}

size_t TextureLoader::pendingCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_pendingTasks.size() + m_completedTasks.size();
}

}
