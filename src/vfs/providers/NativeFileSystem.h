#pragma once

#include "../IFileSystemProvider.h"
#include <fstream>
#include <thread>
#include <unordered_set>

namespace spt3d {

class NativeFileSystemProvider : public IFileSystemProvider {
public:
    explicit NativeFileSystemProvider(const std::string& rootPath);
    ~NativeFileSystemProvider();
    
    const char* scheme() const override { return "file"; }
    
    uint64_t read(const std::string& path, 
                  ThreadSafeQueue<FsResult>& q, 
                  std::function<void(const uint8_t*, size_t, bool)> cb) override;
    uint64_t write(const std::string& path, 
                   const uint8_t* data, size_t size,
                   ThreadSafeQueue<FsResult>& q, 
                   std::function<void(const uint8_t*, size_t, bool)> cb) override;
    void cancel(uint64_t id) override;

private:
    std::string m_rootPath;
    std::atomic<uint64_t> m_nextId{1};
    std::unordered_set<uint64_t> m_cancelled;
    std::mutex m_mutex;
    
    std::thread m_thread;
    ThreadSafeQueue<std::function<void()>> m_tasks;
    std::atomic<bool> m_running{false};
    
    void startThread();
    void stopThread();
    std::string resolvePath(const std::string& path);
};

}
