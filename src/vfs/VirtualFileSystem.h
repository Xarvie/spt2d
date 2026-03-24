#pragma once

#include "IFileSystemProvider.h"
#include "../core/ThreadSafeQueue.h"

#include <unordered_map>
#include <memory>
#include <string>
#include <atomic>
#include <mutex>

namespace spt3d {

class VirtualFileSystem {
public:
    static VirtualFileSystem& Instance();
    
    void mount(const std::string& scheme, std::unique_ptr<IFileSystemProvider> provider);
    
    uint64_t read(const std::string& path, 
                  std::function<void(const uint8_t*, size_t, bool)> cb);
    uint64_t write(const std::string& path, 
                   const uint8_t* data, size_t size,
                   std::function<void(const uint8_t*, size_t, bool)> cb);
    
    void cancel(uint64_t id);
    void cancelAll();
    void processCompleted();

private:
    VirtualFileSystem() = default;
    ~VirtualFileSystem() = default;
    VirtualFileSystem(const VirtualFileSystem&) = delete;
    VirtualFileSystem& operator=(const VirtualFileSystem&) = delete;
    
    std::unordered_map<std::string, std::unique_ptr<IFileSystemProvider>> m_providers;
    ThreadSafeQueue<FsResult> m_resultQueue;
    std::atomic<uint64_t> m_nextId{1};
    std::unordered_map<uint64_t, bool> m_cancelled;
    std::mutex m_mutex;
    
    IFileSystemProvider* getProvider(const std::string& path);
    std::string parsePath(const std::string& path, std::string& scheme);
};

}
