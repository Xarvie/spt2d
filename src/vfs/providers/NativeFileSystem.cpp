#include "NativeFileSystem.h"
#include <fstream>
#include <filesystem>

namespace spt3d {

NativeFileSystemProvider::NativeFileSystemProvider(const std::string& rootPath)
    : m_rootPath(rootPath) {
    startThread();
}

NativeFileSystemProvider::~NativeFileSystemProvider() {
    stopThread();
}

void NativeFileSystemProvider::startThread() {
    m_running = true;
    m_thread = std::thread([this]() {
        std::vector<std::function<void()>> tasks;
        while (m_running) {
            tasks.clear();
            m_tasks.drainTo(tasks);
            for (auto& task : tasks) {
                task();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });
}

void NativeFileSystemProvider::stopThread() {
    m_running = false;
    m_tasks.close();
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

std::string NativeFileSystemProvider::resolvePath(const std::string& path) {
    std::filesystem::path fullPath = std::filesystem::path(m_rootPath) / path;
    return fullPath.string();
}

uint64_t NativeFileSystemProvider::read(const std::string& path, 
                                         ThreadSafeQueue<FsResult>& q, 
                                         std::function<void(const uint8_t*, size_t, bool)> cb) {
    uint64_t id = ++m_nextId;
    std::string fullPath = resolvePath(path);
    
    m_tasks.push([id, fullPath, &q, cb, this]() {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_cancelled.count(id) > 0) {
                m_cancelled.erase(id);
                return;
            }
        }
        
        FsResult result;
        result.id = id;
        result.callback = cb;
        
        std::ifstream file(fullPath, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            result.success = false;
            result.error = "Failed to open file: " + fullPath;
            q.push(std::move(result));
            return;
        }
        
        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);
        
        result.data.resize(size);
        if (file.read(reinterpret_cast<char*>(result.data.data()), size)) {
            result.success = true;
        } else {
            result.success = false;
            result.error = "Failed to read file: " + fullPath;
        }
        
        q.push(std::move(result));
    });
    
    return id;
}

uint64_t NativeFileSystemProvider::write(const std::string& path, 
                                          const uint8_t* data, size_t size,
                                          ThreadSafeQueue<FsResult>& q, 
                                          std::function<void(const uint8_t*, size_t, bool)> cb) {
    uint64_t id = ++m_nextId;
    std::string fullPath = resolvePath(path);
    
    std::vector<uint8_t> dataCopy(data, data + size);
    m_tasks.push([id, fullPath, dataCopy, &q, cb, this]() mutable {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_cancelled.count(id) > 0) {
                m_cancelled.erase(id);
                return;
            }
        }
        
        FsResult result;
        result.id = id;
        result.callback = cb;
        
        std::filesystem::path p(fullPath);
        std::filesystem::create_directories(p.parent_path());
        
        std::ofstream file(fullPath, std::ios::binary);
        if (!file.is_open()) {
            result.success = false;
            result.error = "Failed to create file: " + fullPath;
            q.push(std::move(result));
            return;
        }
        
        if (file.write(reinterpret_cast<const char*>(dataCopy.data()), dataCopy.size())) {
            result.success = true;
        } else {
            result.success = false;
            result.error = "Failed to write file: " + fullPath;
        }
        
        q.push(std::move(result));
    });
    
    return id;
}

void NativeFileSystemProvider::cancel(uint64_t id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (id == 0) {
        m_cancelled.clear();
    } else {
        m_cancelled.insert(id);
    }
}

}
