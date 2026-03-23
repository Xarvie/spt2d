#pragma once

#include <cstdint>

namespace spt {

struct ThreadConfig {
    bool multithreaded = true;
    int logicFps = 30;
    int renderFps = 60;
    
    static ThreadConfig singleThread() {
        ThreadConfig cfg;
        cfg.multithreaded = false;
        cfg.logicFps = 60;
        cfg.renderFps = 60;
        return cfg;
    }
    
    static ThreadConfig multiThread(int logicFps = 30, int renderFps = 60) {
        ThreadConfig cfg;
        cfg.multithreaded = true;
        cfg.logicFps = logicFps;
        cfg.renderFps = renderFps;
        return cfg;
    }
};

}
