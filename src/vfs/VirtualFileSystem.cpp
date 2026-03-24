#include "VirtualFileSystem.h"
#include <algorithm>

namespace spt3d {

VirtualFileSystem& VirtualFileSystem::Instance() {
    static VirtualFileSystem instance;
    return instance;
}

void VirtualFileSystem::mount(const std::string& scheme, std::unique_ptr<IFileSystemProvider> provider) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_providers[scheme] = std::move(provider);
}

uint64_t VirtualFileSystem::read(const std::string& path, 
                                  std::function<void(const uint8_t*, size_t, bool)> cb) {
    std::string scheme;
    std::string realPath = parsePath(path, scheme);
    
    auto* provider = getProvider(path);
    if (!provider) {
        if (cb) {
            FsResult result;
            result.id = ++m_nextId;
            result.success = false;
            result.error = "No provider for scheme: " + scheme;
            result.callback = cb;
            m_resultQueue.push(std::move(result));
        }
        return 0;
    }
    
    return provider->read(realPath, m_resultQueue, cb);
}

uint64_t VirtualFileSystem::write(const std::string& path, 
                                   const uint8_t* data, size_t size,
                                   std::function<void(const uint8_t*, size_t, bool)> cb) {
    std::string scheme;
    std::string realPath = parsePath(path, scheme);
    
    auto* provider = getProvider(path);
    if (!provider) {
        if (cb) {
            FsResult result;
            result.id = ++m_nextId;
            result.success = false;
            result.error = "No provider for scheme: " + scheme;
            result.callback = cb;
            m_resultQueue.push(std::move(result));
        }
        return 0;
    }
    
    return provider->write(realPath, data, size, m_resultQueue, cb);
}

void VirtualFileSystem::cancel(uint64_t id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_cancelled[id] = true;
    
    for (auto& pair : m_providers) {
        pair.second->cancel(id);
    }
}

void VirtualFileSystem::cancelAll() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_cancelled.clear();
    
    for (auto& pair : m_providers) {
        pair.second->cancel(0);
    }
}

void VirtualFileSystem::processCompleted() {
    FsResult result;
    while (m_resultQueue.tryPop(result)) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_cancelled.count(result.id) > 0) {
                m_cancelled.erase(result.id);
                continue;
            }
        }
        
        if (result.callback) {
            result.callback(result.success ? result.data.data() : nullptr,
                           result.data.size(),
                           result.success);
        }
    }
}

IFileSystemProvider* VirtualFileSystem::getProvider(const std::string& path) {
    size_t pos = path.find("://");
    if (pos == std::string::npos) {
        return nullptr;
    }
    
    std::string scheme = path.substr(0, pos);
    std::transform(scheme.begin(), scheme.end(), scheme.begin(), ::tolower);
    
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_providers.find(scheme);
    if (it != m_providers.end()) {
        return it->second.get();
    }
    return nullptr;
}

std::string VirtualFileSystem::parsePath(const std::string& path, std::string& scheme) {
    size_t pos = path.find("://");
    if (pos == std::string::npos) {
        scheme = "";
        return path;
    }
    
    scheme = path.substr(0, pos);
    std::transform(scheme.begin(), scheme.end(), scheme.begin(), ::tolower);
    return path.substr(pos + 3);
}

}
