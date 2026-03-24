#include "../Spt3D.h"
#include "VirtualFileSystem.h"
#include "providers/NativeFileSystem.h"

namespace spt3d {

void MountAssets(std::string_view path) {
#if defined(__WXGAME__)
#elif defined(__EMSCRIPTEN__)
#else
    spt3d::VirtualFileSystem::Instance().mount("assets", 
        std::make_unique<spt3d::NativeFileSystemProvider>(std::string(path)));
#endif
}

void MountUser(std::string_view path) {
#if defined(__WXGAME__)
#else
    spt3d::VirtualFileSystem::Instance().mount("user", 
        std::make_unique<spt3d::NativeFileSystemProvider>(std::string(path)));
#endif
}

void MountCache(std::string_view path) {
#if defined(__WXGAME__)
#else
    spt3d::VirtualFileSystem::Instance().mount("cache", 
        std::make_unique<spt3d::NativeFileSystemProvider>(std::string(path)));
#endif
}

uint64_t FsRead(std::string_view path, FsCallback cb) {
    return spt3d::VirtualFileSystem::Instance().read(std::string(path), cb);
}

uint64_t FsWrite(std::string_view path, const uint8_t* data, size_t size, FsCallback cb) {
    return spt3d::VirtualFileSystem::Instance().write(std::string(path), data, size, cb);
}

void FsCancel(uint64_t id) {
    spt3d::VirtualFileSystem::Instance().cancel(id);
}

void FsCancelAll() {
    spt3d::VirtualFileSystem::Instance().cancelAll();
}

}
