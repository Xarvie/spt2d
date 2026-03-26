#ifdef __WXGAME__

#include "WxFileSystem.h"
#include <emscripten/emscripten.h>
#include <cstring>
#include <iostream>

namespace spt3d {

namespace {
    struct PendingRead {
        uint32_t id;
        ThreadSafeQueue<FsResult>* queue;
        std::function<void(const uint8_t*, size_t, bool)> callback;
    };

    struct PendingWrite {
        uint32_t id;
        ThreadSafeQueue<FsResult>* queue;
        std::function<void(const uint8_t*, size_t, bool)> callback;
    };

    static std::unordered_map<uint32_t, PendingRead> g_pendingReads;
    static std::unordered_map<uint32_t, PendingWrite> g_pendingWrites;
    static std::mutex g_pendingMutex;

    extern "C" {

    EMSCRIPTEN_KEEPALIVE
    void WxVfs_OnReadComplete(uint32_t id, int success, const uint8_t* data, int size, const char* errMsg) {
        std::cout << "[WxVfs] OnReadComplete: id=" << id << ", success=" << success << ", size=" << size << std::endl;
        std::lock_guard<std::mutex> lock(g_pendingMutex);
        auto it = g_pendingReads.find(id);
        if (it == g_pendingReads.end()) {
            std::cout << "[WxVfs] OnReadComplete: id=" << id << " NOT FOUND in pending reads!" << std::endl;
            return;
        }

        auto pending = std::move(it->second);
        g_pendingReads.erase(it);

        FsResult result;
        result.id = id;
        result.success = (success != 0);
        result.callback = std::move(pending.callback);

        if (success && data && size > 0) {
            result.data.assign(data, data + size);
        } else if (!success) {
            result.error = errMsg ? errMsg : "Read failed";
            std::cout << "[WxVfs] OnReadComplete ERROR: " << result.error << std::endl;
        }

        pending.queue->push(std::move(result));
    }

    EMSCRIPTEN_KEEPALIVE
    void WxVfs_OnWriteComplete(uint32_t id, int success, const char* errMsg) {
        std::lock_guard<std::mutex> lock(g_pendingMutex);
        auto it = g_pendingWrites.find(id);
        if (it == g_pendingWrites.end()) return;

        auto pending = std::move(it->second);
        g_pendingWrites.erase(it);

        FsResult result;
        result.id = id;
        result.success = (success != 0);
        result.callback = std::move(pending.callback);

        if (!success) {
            result.error = errMsg ? errMsg : "Write failed";
        }

        pending.queue->push(std::move(result));
    }

    }
}

WxFileSystemProvider::WxFileSystemProvider(RootType type)
    : m_type(type) {
    initRootPath();
    std::cout << "[WxVfs] Provider created: type=" << static_cast<int>(type) 
              << ", scheme=" << scheme() 
              << ", rootPath=" << m_rootPath << std::endl;
}

const char* WxFileSystemProvider::scheme() const {
    return SchemeForType(m_type);
}

const char* WxFileSystemProvider::SchemeForType(RootType type) {
    switch (type) {
        case RootType::UserData: return "userdata";
        case RootType::Cache:    return "cache";
        case RootType::Package:  return "package";
        default: return "unknown";
    }
}

void WxFileSystemProvider::initRootPath() {
    char buf[512] = {0};
    EM_ASM({
        if (typeof wx !== "undefined" && wx.env) {
            var path = "";
            if ($0 == 0) path = wx.env.USER_DATA_PATH || "";
            else if ($0 == 1) path = wx.env.USER_DATA_PATH ? (wx.env.USER_DATA_PATH + "/cache") : "";
            else if ($0 == 2) path = "";
            if (path) stringToUTF8(path, $1, 512);
        }
    }, static_cast<int>(m_type), buf);
    m_rootPath = buf;
}

std::string WxFileSystemProvider::resolvePath(const std::string& path) {
    if (m_type == RootType::Package) {
        return path;
    }
    if (m_rootPath.empty()) {
        return path;
    }
    if (path.empty() || path[0] == '/') {
        return m_rootPath + path;
    }
    return m_rootPath + "/" + path;
}

uint64_t WxFileSystemProvider::read(const std::string& path,
                                     ThreadSafeQueue<FsResult>& q,
                                     std::function<void(const uint8_t*, size_t, bool)> cb) {
    uint32_t id = static_cast<uint32_t>(++m_nextId);
    std::string fullPath = resolvePath(path);
    
    std::cout << "[WxVfs] read() called: path=" << path << ", fullPath=" << fullPath << ", id=" << id << std::endl;

    {
        std::lock_guard<std::mutex> lock(g_pendingMutex);
        g_pendingReads[id] = {id, &q, cb};
    }

    EM_ASM({
        var id = $0;
        var fullPath = UTF8ToString($1);
        var isPackage = $2;
        
        console.log("[WxVfs] readFile: " + fullPath + ", isPackage=" + isPackage);
        
        var fs = typeof wx !== "undefined" ? wx.getFileSystemManager() : null;
        if (!fs) {
            console.log("[WxVfs] ERROR: wx not available");
            _WxVfs_OnReadComplete(id, 0, 0, 0, "wx not available");
            return;
        }
        
        var filePath = fullPath;
        
        if (isPackage) {
            console.log("[WxVfs] Package resource detected, using direct path: " + filePath);
        }
        
        fs.readFile({
            filePath: filePath,
            success: function(res) {
                console.log("[WxVfs] readFile SUCCESS: " + filePath + ", size=" + (res.data ? res.data.byteLength : 0));
                var data = res.data;
                var u8;
                if (data instanceof ArrayBuffer) {
                    u8 = new Uint8Array(data);
                } else if (typeof data === "string") {
                    var enc = new TextEncoder();
                    u8 = enc.encode(data);
                } else {
                    u8 = new Uint8Array(0);
                }
                var ptr = _malloc(u8.length);
                HEAPU8.set(u8, ptr);
                _WxVfs_OnReadComplete(id, 1, ptr, u8.length, "");
                _free(ptr);
            },
            fail: function(err) {
                console.log("[WxVfs] readFile FAIL: " + filePath + ", err=" + (err.errMsg || "unknown"));
                var msg = err.errMsg || "read failed";
                var mLen = lengthBytesUTF8(msg) + 1;
                var mPtr = _malloc(mLen);
                stringToUTF8(msg, mPtr, mLen);
                _WxVfs_OnReadComplete(id, 0, 0, 0, mPtr);
                _free(mPtr);
            }
        });
    }, id, fullPath.c_str(), (m_type == RootType::Package) ? 1 : 0);

    return id;
}

uint64_t WxFileSystemProvider::write(const std::string& path,
                                      const uint8_t* data, size_t size,
                                      ThreadSafeQueue<FsResult>& q,
                                      std::function<void(const uint8_t*, size_t, bool)> cb) {
    if (m_type == RootType::Package) {
        FsResult result;
        result.id = ++m_nextId;
        result.success = false;
        result.error = "Package filesystem is read-only";
        result.callback = cb;
        q.push(std::move(result));
        return result.id;
    }

    uint32_t id = static_cast<uint32_t>(++m_nextId);
    std::string fullPath = resolvePath(path);

    {
        std::lock_guard<std::mutex> lock(g_pendingMutex);
        g_pendingWrites[id] = {id, &q, cb};
    }

    EM_ASM({
        var id = $0;
        var fullPath = UTF8ToString($1);
        var dataPtr = $2;
        var dataSize = $3;
        var fs = typeof wx !== "undefined" ? wx.getFileSystemManager() : null;
        if (!fs) {
            _WxVfs_OnWriteComplete(id, 0, "wx not available");
            return;
        }

        var u8 = new Uint8Array(dataSize);
        for (var i = 0; i < dataSize; i++) {
            u8[i] = HEAPU8[dataPtr + i];
        }

        var dirPath = fullPath.substring(0, fullPath.lastIndexOf("/"));
        try {
            fs.accessSync(dirPath);
        } catch(e) {
            try {
                fs.mkdirSync(dirPath, true);
            } catch(mkdirErr) {}
        }

        fs.writeFile({
            filePath: fullPath,
            data: u8.buffer,
            success: function() {
                _WxVfs_OnWriteComplete(id, 1, "");
            },
            fail: function(err) {
                var msg = err.errMsg || "write failed";
                var mLen = lengthBytesUTF8(msg) + 1;
                var mPtr = _malloc(mLen);
                stringToUTF8(msg, mPtr, mLen);
                _WxVfs_OnWriteComplete(id, 0, mPtr);
                _free(mPtr);
            }
        });
    }, id, fullPath.c_str(), data, static_cast<int>(size));

    return id;
}

void WxFileSystemProvider::cancel(uint64_t id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (id == 0) {
        m_cancelled.clear();
        std::lock_guard<std::mutex> plock(g_pendingMutex);
        g_pendingReads.clear();
        g_pendingWrites.clear();
    } else {
        m_cancelled[id] = true;
        std::lock_guard<std::mutex> plock(g_pendingMutex);
        g_pendingReads.erase(static_cast<uint32_t>(id));
        g_pendingWrites.erase(static_cast<uint32_t>(id));
    }
}

}
#endif
