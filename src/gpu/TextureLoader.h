#pragma once

#include "../Handle.h"
#include "GPUDevice.h"

#include <string>
#include <functional>
#include <queue>
#include <mutex>
#include <vector>

namespace spt3d {

struct TextureLoadResult {
    TexHandle handle;
    std::string path;
    bool success = false;
    std::string error;
};

class TextureLoader {
public:
    using LoadCallback = std::function<void(const TextureLoadResult&)>;

    static TextureLoader& Instance();

    void initialize(GPUDevice* gpu);
    void shutdown();

    void loadAsync(const std::string& path, 
                   LoadCallback callback,
                   bool genMips = true);

    TextureLoadResult loadFromMemory(const uint8_t* data, size_t size, 
                                       bool genMips = true);

    TextureLoadResult loadFromPixels(const uint8_t* pixels, int width, int height,
                                       int channels, bool genMips = true);

    void update();

    size_t pendingCount() const;

private:
    TextureLoader() = default;
    ~TextureLoader() = default;
    TextureLoader(const TextureLoader&) = delete;
    TextureLoader& operator=(const TextureLoader&) = delete;

    struct PendingTask {
        std::string path;
        LoadCallback callback;
        bool genMips = true;
        std::vector<uint8_t> fileData;
        bool dataReady = false;
    };

    struct CompletedTask {
        TextureLoadResult result;
        LoadCallback callback;
    };

    GPUDevice* m_gpu = nullptr;
    std::queue<PendingTask> m_pendingTasks;
    std::queue<CompletedTask> m_completedTasks;
    mutable std::mutex m_mutex;
};

}
