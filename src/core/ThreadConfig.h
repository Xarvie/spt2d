// spt3d/core/ThreadConfig.h — Thread model configuration
// [THREAD: any] — Pure data, read before threads are spawned.
#pragma once

#include <cstdint>
#include <string>
#include <cassert>

namespace spt3d {

enum class FrameRateMode : uint8_t {
    Unlocked = 0,
    FixedHz,
    Vsync,
};

struct ThreadConfig {
    bool          multithreaded       = true;
    int           logicHz             = 30;
    int           renderHz            = 60;
    FrameRateMode logicFpsMode        = FrameRateMode::FixedHz;
    FrameRateMode renderFpsMode       = FrameRateMode::FixedHz;
    float         maxDeltaTimeSeconds = 0.1f;

    std::string logicThreadName   = "spt.logic";
    std::string renderThreadName  = "spt.render";
    uint64_t    logicCpuAffinity  = 0;
    uint64_t    renderCpuAffinity = 0;

    static ThreadConfig singleThread(int hz = 60) noexcept {
        ThreadConfig c; c.multithreaded = false;
        c.logicHz = hz; c.renderHz = hz; return c;
    }
    static ThreadConfig multiThread(int lHz = 30, int rHz = 60) noexcept {
        ThreadConfig c; c.logicHz = lHz; c.renderHz = rHz; return c;
    }
    static ThreadConfig multiThreadVsync(int lHz = 30) noexcept {
        ThreadConfig c; c.logicHz = lHz; c.renderHz = 0;
        c.renderFpsMode = FrameRateMode::Vsync; return c;
    }

    [[nodiscard]] float logicDeltaTime()  const noexcept {
        return (logicFpsMode == FrameRateMode::FixedHz && logicHz > 0) ? 1.0f / float(logicHz) : 0.0f;
    }
    [[nodiscard]] float renderDeltaTime() const noexcept {
        return (renderFpsMode == FrameRateMode::FixedHz && renderHz > 0) ? 1.0f / float(renderHz) : 0.0f;
    }
    [[nodiscard]] float clampDelta(float dt) const noexcept {
        return dt < maxDeltaTimeSeconds ? dt : maxDeltaTimeSeconds;
    }
    [[nodiscard]] bool validate() const noexcept {
        if (maxDeltaTimeSeconds <= 0.0f) return false;
        if (logicFpsMode  == FrameRateMode::FixedHz && logicHz  <= 0) return false;
        if (renderFpsMode == FrameRateMode::FixedHz && renderHz <= 0) return false;
        if (!multithreaded && renderFpsMode == FrameRateMode::Vsync) return false;
        return true;
    }
    void assertValid() const noexcept { assert(validate()); }
};

} // namespace spt3d
