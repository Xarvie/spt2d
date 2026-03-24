#pragma once

#include "../core/ThreadSafeQueue.h"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace spt3d {

struct FsResult {
    uint64_t id = 0;
    bool success = false;
    std::string error;
    std::vector<uint8_t> data;
    std::function<void(const uint8_t*, size_t, bool)> callback;
};

class IFileSystemProvider {
public:
    virtual ~IFileSystemProvider() = default;
    virtual const char* scheme() const = 0;
    virtual uint64_t read(const std::string& path, 
                          ThreadSafeQueue<FsResult>& resultQueue,
                          std::function<void(const uint8_t*, size_t, bool)> cb) = 0;
    virtual uint64_t write(const std::string& path,
                           const uint8_t* data, size_t size,
                           ThreadSafeQueue<FsResult>& resultQueue,
                           std::function<void(const uint8_t*, size_t, bool)> cb) = 0;
    virtual void cancel(uint64_t id) = 0;
};

}
