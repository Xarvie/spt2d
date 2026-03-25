#pragma once

#ifdef __WXGAME__

#include "../IFileSystemProvider.h"
#include <unordered_map>
#include <string>

namespace spt3d {

class WxFileSystemProvider : public IFileSystemProvider {
public:
    enum class RootType {
        UserData,   
        Cache,      
        Package     
    };

    explicit WxFileSystemProvider(RootType type);
    ~WxFileSystemProvider() override = default;

    const char* scheme() const override;

    uint64_t read(const std::string& path,
                  ThreadSafeQueue<FsResult>& q,
                  std::function<void(const uint8_t*, size_t, bool)> cb) override;

    uint64_t write(const std::string& path,
                   const uint8_t* data, size_t size,
                   ThreadSafeQueue<FsResult>& q,
                   std::function<void(const uint8_t*, size_t, bool)> cb) override;

    void cancel(uint64_t id) override;

    static const char* SchemeForType(RootType type);

private:
    RootType m_type;
    std::string m_rootPath;
    std::atomic<uint64_t> m_nextId{1};
    std::unordered_map<uint64_t, bool> m_cancelled;
    std::mutex m_mutex;

    void initRootPath();
    std::string resolvePath(const std::string& path);
};

}

#endif
