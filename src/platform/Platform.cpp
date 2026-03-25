#include "Platform.h"

namespace spt3d {

std::unique_ptr<IPlatformHub> createPlatform() {
#if defined(__WXGAME__)
    return detail::createPlatform_Wx();
#elif defined(__EMSCRIPTEN__)
    return detail::createPlatform_Web();
#else
    return detail::createPlatform_Sdl3();
#endif
}

} // namespace spt3d
