#pragma once

#include "ResHandle.h"
#include "IResource.h"
#include <vector>
#include <queue>
#include <functional>
#include <mutex>
#include <iostream>

namespace spt3d {

struct ResourceSlot {
    IResource* resource = nullptr;
    uint8_t generation = 0;
    ResType type = ResType::Invalid;
    uint8_t tag = 0;

    bool isFree() const { return resource == nullptr; }
};

class ResourceManager {
public:
    using GpuTask = std::function<void()>;

    static ResourceManager& Instance();

    ResourceManager();
    ~ResourceManager();

    ResourceManager(const ResourceManager&) = delete;
    ResourceManager& operator=(const ResourceManager&) = delete;

    void init();
    void update();
    void cleanup();

    template<typename T, typename... Args>
    ResHandle createResource(Args&&... args) {
        static_assert(std::is_base_of<IResource, T>::value, "T must derive from IResource");
        
        std::lock_guard<std::mutex> lk(m_mutex);

        uint16_t index = allocateSlotInternal(T::StaticType);
        if (index == 0) return ResHandle::Invalid();

        T* res = new T(std::forward<Args>(args)...);
        ResourceSlot& slot = m_slots[index];

        res->setGpuDispatcher([this](GpuTask task) {
            std::lock_guard<std::mutex> glk(m_gpuMutex);
            m_gpuTasks.push(std::move(task));
        });

        slot.resource = res;
        slot.type = T::StaticType;

        ResHandle handle = ResHandle::make(index, slot.generation, slot.tag);
        res->setResHandle(handle);

        return handle;
    }

    template<typename T>
    T* get(ResHandle handle) {
        IResource* res = getResource(handle);
        if (!res) return nullptr;
#ifdef _DEBUG
        return dynamic_cast<T*>(res);
#else
        return static_cast<T*>(res);
#endif
    }

    IResource* getResource(ResHandle handle);

    bool isValid(ResHandle handle) const;
    bool isReady(ResHandle handle) const;
    ResState getState(ResHandle handle) const;

    void addRef(ResHandle handle);
    void release(ResHandle handle);
    bool unload(ResHandle handle);

    void setTag(ResHandle handle, uint8_t tag);
    uint8_t getTag(ResHandle handle) const;
    size_t unloadByTag(uint8_t tag);

    size_t garbageCollect();

    void fetchPendingGpuTasks(std::queue<GpuTask>& outQueue);

    size_t getResourceCount() const;
    size_t getMemoryUsage() const;
    void printDebugInfo() const;

private:
    std::vector<ResourceSlot> m_slots;
    std::vector<uint16_t> m_freeList;

    mutable std::mutex m_mutex;
    std::mutex m_gpuMutex;
    std::queue<GpuTask> m_gpuTasks;

    static ResourceManager* s_instance;

    uint16_t allocateSlotInternal(ResType type);
    void freeSlotInternal(uint16_t index);
    bool validateHandleInternal(ResHandle handle) const;
};

}
