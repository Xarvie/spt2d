#include "ResourceManager.h"
#include <algorithm>

namespace spt {

ResourceManager* ResourceManager::s_instance = nullptr;

const char* ResTypeToString(ResType type) {
    switch (type) {
        case ResType::Texture: return "Texture";
        case ResType::Font:    return "Font";
        case ResType::Audio:   return "Audio";
        case ResType::RawData: return "RawData";
        case ResType::Atlas:   return "Atlas";
        case ResType::Spine:   return "Spine";
        default:               return "Invalid";
    }
}

const char* ResStateToString(ResState state) {
    switch (state) {
        case ResState::Unloaded: return "Unloaded";
        case ResState::Loading:  return "Loading";
        case ResState::Loaded:   return "Loaded";
        case ResState::Ready:    return "Ready";
        case ResState::Failed:   return "Failed";
        default:                 return "Unknown";
    }
}

ResourceManager::ResourceManager() {
    if (s_instance) {
        std::cerr << "[ResourceManager] FATAL: Duplicate instance!" << std::endl;
        std::abort();
    }
    s_instance = this;

    m_slots.resize(1);
    std::cout << "[ResourceManager] Initialized" << std::endl;
}

ResourceManager::~ResourceManager() {
    cleanup();

    for (auto& slot : m_slots) {
        if (slot.resource) {
            delete slot.resource;
            slot.resource = nullptr;
        }
    }

    if (s_instance == this) {
        s_instance = nullptr;
    }
    std::cout << "[ResourceManager] Destroyed" << std::endl;
}

ResourceManager& ResourceManager::Instance() {
    return *s_instance;
}

void ResourceManager::init() {
}

void ResourceManager::update() {
    static int frameCounter = 0;
    if (++frameCounter % 600 == 0) {
        size_t freed = garbageCollect();
        if (freed > 0) {
            std::cout << "[ResourceManager] GC freed " << freed << " resources" << std::endl;
        }
    }
}

void ResourceManager::cleanup() {
    std::lock_guard<std::mutex> lk(m_mutex);

    for (auto& slot : m_slots) {
        if (slot.resource) {
            slot.resource->cleanupGPU();
        }
    }
}

IResource* ResourceManager::getResource(ResHandle handle) {
    std::lock_guard<std::mutex> lk(m_mutex);
    
    if (!validateHandleInternal(handle)) return nullptr;
    return m_slots[handle.getIndex()].resource;
}

bool ResourceManager::isValid(ResHandle handle) const {
    std::lock_guard<std::mutex> lk(m_mutex);
    return validateHandleInternal(handle);
}

bool ResourceManager::isReady(ResHandle handle) const {
    std::lock_guard<std::mutex> lk(m_mutex);
    
    if (!validateHandleInternal(handle)) return false;
    const IResource* res = m_slots[handle.getIndex()].resource;
    return res && res->getState() == ResState::Ready;
}

ResState ResourceManager::getState(ResHandle handle) const {
    std::lock_guard<std::mutex> lk(m_mutex);
    
    if (!validateHandleInternal(handle)) return ResState::Failed;
    const IResource* res = m_slots[handle.getIndex()].resource;
    return res ? res->getState() : ResState::Failed;
}

void ResourceManager::addRef(ResHandle handle) {
    std::lock_guard<std::mutex> lk(m_mutex);
    
    if (!validateHandleInternal(handle)) return;
    m_slots[handle.getIndex()].resource->addRef();
}

void ResourceManager::release(ResHandle handle) {
    std::lock_guard<std::mutex> lk(m_mutex);
    
    if (!validateHandleInternal(handle)) return;
    m_slots[handle.getIndex()].resource->release();
}

bool ResourceManager::unload(ResHandle handle) {
    std::lock_guard<std::mutex> lk(m_mutex);
    
    if (!validateHandleInternal(handle)) return false;
    freeSlotInternal(handle.getIndex());
    return true;
}

void ResourceManager::setTag(ResHandle handle, uint8_t tag) {
    std::lock_guard<std::mutex> lk(m_mutex);
    
    if (!validateHandleInternal(handle)) return;
    m_slots[handle.getIndex()].tag = tag;
}

uint8_t ResourceManager::getTag(ResHandle handle) const {
    std::lock_guard<std::mutex> lk(m_mutex);
    
    if (!validateHandleInternal(handle)) return 0;
    return m_slots[handle.getIndex()].tag;
}

size_t ResourceManager::unloadByTag(uint8_t tag) {
    std::lock_guard<std::mutex> lk(m_mutex);

    size_t count = 0;
    for (size_t i = 1; i < m_slots.size(); ++i) {
        ResourceSlot& slot = m_slots[i];
        if (!slot.isFree() && slot.tag == tag) {
            freeSlotInternal(static_cast<uint16_t>(i));
            ++count;
        }
    }
    return count;
}

size_t ResourceManager::garbageCollect() {
    std::lock_guard<std::mutex> lk(m_mutex);

    size_t count = 0;
    for (size_t i = 1; i < m_slots.size(); ++i) {
        ResourceSlot& slot = m_slots[i];
        if (!slot.isFree() && slot.resource && slot.resource->getRefCount() <= 1) {
            freeSlotInternal(static_cast<uint16_t>(i));
            ++count;
        }
    }
    return count;
}

void ResourceManager::fetchPendingGpuTasks(std::queue<GpuTask>& outQueue) {
    std::lock_guard<std::mutex> lk(m_gpuMutex);
    outQueue.swap(m_gpuTasks);
}

size_t ResourceManager::getResourceCount() const {
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_slots.size() - m_freeList.size() - 1;
}

size_t ResourceManager::getMemoryUsage() const {
    std::lock_guard<std::mutex> lk(m_mutex);

    size_t total = 0;
    for (const auto& slot : m_slots) {
        if (!slot.isFree() && slot.resource) {
            total += slot.resource->getMemoryUsage();
        }
    }
    return total;
}

void ResourceManager::printDebugInfo() const {
    std::lock_guard<std::mutex> lk(m_mutex);

    size_t usedSlots = m_slots.size() - m_freeList.size() - 1;
    
    size_t totalMemory = 0;
    for (const auto& slot : m_slots) {
        if (!slot.isFree() && slot.resource) {
            totalMemory += slot.resource->getMemoryUsage();
        }
    }

    std::cout << "===== ResourceManager Debug =====" << std::endl;
    std::cout << "Total Slots: " << m_slots.size() << std::endl;
    std::cout << "Used Slots: " << usedSlots << std::endl;
    std::cout << "Free List: " << m_freeList.size() << std::endl;
    std::cout << "Memory: " << (totalMemory / 1024.0 / 1024.0) << " MB" << std::endl;
    std::cout << "=================================" << std::endl;
}

uint16_t ResourceManager::allocateSlotInternal(ResType type) {
    uint16_t index;
    if (!m_freeList.empty()) {
        index = m_freeList.back();
        m_freeList.pop_back();
    } else {
        if (m_slots.size() >= 65535) {
            std::cerr << "[ResourceManager] Slot pool exhausted!" << std::endl;
            return 0;
        }
        index = static_cast<uint16_t>(m_slots.size());
        m_slots.emplace_back();
    }

    ResourceSlot& slot = m_slots[index];
    slot.type = type;
    slot.tag = 0;

    return index;
}

void ResourceManager::freeSlotInternal(uint16_t index) {
    if (index == 0 || index >= m_slots.size()) return;

    ResourceSlot& slot = m_slots[index];

    if (slot.resource) {
        slot.resource->cleanupGPU();
        slot.resource->cleanupCPU();
        delete slot.resource;
        slot.resource = nullptr;
    }

    slot.generation++;
    slot.type = ResType::Invalid;
    slot.tag = 0;

    m_freeList.push_back(index);
}

bool ResourceManager::validateHandleInternal(ResHandle handle) const {
    if (!handle.isValid()) return false;

    uint16_t index = handle.getIndex();
    if (index >= m_slots.size()) return false;

    const ResourceSlot& slot = m_slots[index];
    if (slot.isFree()) return false;

    if (slot.generation != handle.getGeneration()) return false;

    return true;
}

}
