#pragma once

#include <vector>
#include <queue>
#include <functional>
#include <cstdint>
#include <cstring>

namespace spt {

using GpuTask = std::function<void()>;

struct PlatformCommand {
    enum class Type : uint8_t {
        StartIME,
        StopIME,
        SetIMERect,
        SetClipboard
    };
    
    Type type;
    int x = 0, y = 0, w = 0, h = 0;
    const char* textData = nullptr;
};

struct RenderCommand {
    static constexpr size_t kPayloadSize = 128;
    
    using ExecuteFn = void(*)(const void* payload);
    
    uint8_t payload[kPayloadSize] = {};
    ExecuteFn execFn = nullptr;
    uint64_t sortKey = 0;
    uint16_t typeId = 0;
    
    void execute() const {
        if (execFn) execFn(payload);
    }
    
    template<typename T>
    static RenderCommand create(const T& cmdData, ExecuteFn fn, uint64_t key = 0) {
        static_assert(sizeof(T) <= kPayloadSize, "Payload too large");
        static_assert(std::is_trivially_copyable<T>::value, "Must be trivially copyable");
        
        RenderCommand cmd;
        std::memcpy(cmd.payload, &cmdData, sizeof(T));
        cmd.execFn = fn;
        cmd.sortKey = key;
        return cmd;
    }
};

struct GameWork {
    double logicTime = 0.0;
    float deltaTime = 0.0f;
    uint64_t frameIndex = 0;
    
    std::vector<RenderCommand> renderCommands;
    std::vector<uint8_t> uniformPoolData;
    
    std::queue<GpuTask> gpuTasks;
    std::vector<PlatformCommand> platformCommands;
    
    int viewportX = 0;
    int viewportY = 0;
    int viewportW = 0;
    int viewportH = 0;
    bool hasViewport = false;
    
    GameWork() = default;
    
    GameWork(GameWork&& other) noexcept
        : logicTime(other.logicTime),
          deltaTime(other.deltaTime),
          frameIndex(other.frameIndex),
          renderCommands(std::move(other.renderCommands)),
          uniformPoolData(std::move(other.uniformPoolData)),
          gpuTasks(std::move(other.gpuTasks)),
          platformCommands(std::move(other.platformCommands)),
          viewportX(other.viewportX),
          viewportY(other.viewportY),
          viewportW(other.viewportW),
          viewportH(other.viewportH),
          hasViewport(other.hasViewport) {}
    
    GameWork& operator=(GameWork&& other) noexcept {
        if (this != &other) {
            logicTime = other.logicTime;
            deltaTime = other.deltaTime;
            frameIndex = other.frameIndex;
            renderCommands = std::move(other.renderCommands);
            uniformPoolData = std::move(other.uniformPoolData);
            gpuTasks = std::move(other.gpuTasks);
            platformCommands = std::move(other.platformCommands);
            viewportX = other.viewportX;
            viewportY = other.viewportY;
            viewportW = other.viewportW;
            viewportH = other.viewportH;
            hasViewport = other.hasViewport;
        }
        return *this;
    }
    
    GameWork(const GameWork&) = default;
    GameWork& operator=(const GameWork&) = default;
    
    void clear() {
        logicTime = 0.0;
        deltaTime = 0.0f;
        frameIndex = 0;
        renderCommands.clear();
        uniformPoolData.clear();
        std::queue<GpuTask> emptyQueue;
        gpuTasks.swap(emptyQueue);
        platformCommands.clear();
        hasViewport = false;
    }
    
    void swap(GameWork& other) noexcept {
        std::swap(logicTime, other.logicTime);
        std::swap(deltaTime, other.deltaTime);
        std::swap(frameIndex, other.frameIndex);
        renderCommands.swap(other.renderCommands);
        uniformPoolData.swap(other.uniformPoolData);
        gpuTasks.swap(other.gpuTasks);
        platformCommands.swap(other.platformCommands);
        std::swap(viewportX, other.viewportX);
        std::swap(viewportY, other.viewportY);
        std::swap(viewportW, other.viewportW);
        std::swap(viewportH, other.viewportH);
        std::swap(hasViewport, other.hasViewport);
    }
};

}
