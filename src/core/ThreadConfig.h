#pragma once

#include <cstdint>
#include <cassert>
#include <string>

namespace spt {

// =============================================================================
//  FrameRateMode
//
//  Controls how each thread regulates its own loop cadence.
//
//  Unlocked    – spin as fast as possible; maximum throughput, maximum CPU.
//  FixedHz     – target a fixed number of ticks per second using a
//                sleep-until loop; the thread yields when ahead of schedule.
//  Vsync       – render thread syncs to the display refresh (platform-managed;
//                only meaningful for the render thread).
// =============================================================================
enum class FrameRateMode : uint8_t {
    Unlocked = 0,
    FixedHz,
    Vsync,
};


// =============================================================================
//  ThreadConfig
//
//  Immutable configuration for the ThreadModel.  Construct via the named
//  factory methods or fill in fields directly, then validate().
//
//  Fields
//  ───────
//  multithreaded
//      When false a SingleThreadModel is created: logic and rendering run
//      sequentially on the calling (main) thread.  Useful for debugging,
//      profiling, and platforms that forbid background threads.
//
//  logicHz / renderHz
//      Target update rates.  For SingleThreadModel both values are ignored
//      and the loop runs at renderHz only.  For MultiThreadModel the logic
//      thread uses logicHz; the render thread is governed by renderFpsMode.
//
//  logicFpsMode / renderFpsMode
//      How each thread regulates its cadence (see FrameRateMode).
//
//  maxDeltaTimeSeconds
//      Hard cap applied to the measured or fixed delta-time before it is
//      passed to IGameLogic::onUpdate().  Prevents physics explosions when
//      the process is suspended (e.g. debugger breakpoint, OS sleep).
//      Recommended value: 0.1 s (6 frames at 60 Hz).
//
//  logicThreadName / renderThreadName
//      OS-visible thread names for profiling tools (Instruments, RenderDoc,
//      PIX, …).  Empty string leaves the name unchanged.
//
//  logicCpuAffinity / renderCpuAffinity
//      Bitmask of CPU cores the thread may run on (0 = no affinity hint,
//      let the OS decide).  Platform support varies; the ThreadModel
//      implementation applies this on a best-effort basis.
// =============================================================================
struct ThreadConfig {

    // ------------------------------------------------------------------
    // Core settings
    // ------------------------------------------------------------------
    bool          multithreaded   = true;
    int           logicHz         = 30;
    int           renderHz        = 60;
    FrameRateMode logicFpsMode    = FrameRateMode::FixedHz;
    FrameRateMode renderFpsMode   = FrameRateMode::FixedHz;

    // ------------------------------------------------------------------
    // Safety
    // ------------------------------------------------------------------
    float maxDeltaTimeSeconds = 0.1f;   ///< dt cap; must be > 0

    // ------------------------------------------------------------------
    // Diagnostics / platform hints
    // ------------------------------------------------------------------
    std::string logicThreadName   = "spt.logic";
    std::string renderThreadName  = "spt.render";
    uint64_t    logicCpuAffinity  = 0;  ///< 0 = no hint
    uint64_t    renderCpuAffinity = 0;  ///< 0 = no hint

    // ------------------------------------------------------------------
    // Named constructors
    // ------------------------------------------------------------------

    /// Single-threaded: logic + render run sequentially on the main thread.
    /// The loop targets `hz` frames per second.
    static ThreadConfig singleThread(int hz = 60) noexcept {
        ThreadConfig c;
        c.multithreaded  = false;
        c.logicHz        = hz;
        c.renderHz       = hz;
        c.logicFpsMode   = FrameRateMode::FixedHz;
        c.renderFpsMode  = FrameRateMode::FixedHz;
        return c;
    }

    /// Multi-threaded fixed-step: logic at `logicHz`, render at `renderHz`.
    static ThreadConfig multiThread(int logicHz  = 30,
                                    int renderHz = 60) noexcept {
        ThreadConfig c;
        c.multithreaded  = true;
        c.logicHz        = logicHz;
        c.renderHz       = renderHz;
        c.logicFpsMode   = FrameRateMode::FixedHz;
        c.renderFpsMode  = FrameRateMode::FixedHz;
        return c;
    }

    /// Multi-threaded with VSync on the render thread.
    static ThreadConfig multiThreadVsync(int logicHz = 30) noexcept {
        ThreadConfig c;
        c.multithreaded  = true;
        c.logicHz        = logicHz;
        c.renderHz       = 0;   // not used when mode == Vsync
        c.logicFpsMode   = FrameRateMode::FixedHz;
        c.renderFpsMode  = FrameRateMode::Vsync;
        return c;
    }

    /// Unlocked: both threads run as fast as possible.  For benchmarking.
    static ThreadConfig unlocked() noexcept {
        ThreadConfig c;
        c.multithreaded  = true;
        c.logicHz        = 0;
        c.renderHz       = 0;
        c.logicFpsMode   = FrameRateMode::Unlocked;
        c.renderFpsMode  = FrameRateMode::Unlocked;
        return c;
    }

    // ------------------------------------------------------------------
    // Derived helpers
    // ------------------------------------------------------------------

    /// Fixed logic time-step in seconds.  Returns 0 for Unlocked mode.
    [[nodiscard]] float logicDeltaTime() const noexcept {
        return (logicFpsMode == FrameRateMode::FixedHz && logicHz > 0)
               ? 1.0f / static_cast<float>(logicHz)
               : 0.0f;
    }

    /// Fixed render interval in seconds.  Returns 0 for Vsync / Unlocked.
    [[nodiscard]] float renderDeltaTime() const noexcept {
        return (renderFpsMode == FrameRateMode::FixedHz && renderHz > 0)
               ? 1.0f / static_cast<float>(renderHz)
               : 0.0f;
    }

    /// Clamp an arbitrary measured dt to the configured safety cap.
    [[nodiscard]] float clampDelta(float dt) const noexcept {
        return (dt < maxDeltaTimeSeconds) ? dt : maxDeltaTimeSeconds;
    }

    // ------------------------------------------------------------------
    // Validation
    // ------------------------------------------------------------------

    /// Returns true when the configuration is self-consistent.
    /// Call this after constructing a config to catch misconfiguration early.
    [[nodiscard]] bool validate() const noexcept {
        if (maxDeltaTimeSeconds <= 0.0f)                           return false;
        if (logicFpsMode == FrameRateMode::FixedHz && logicHz <= 0)  return false;
        if (renderFpsMode == FrameRateMode::FixedHz && renderHz <= 0) return false;
        if (!multithreaded &&
            renderFpsMode == FrameRateMode::Vsync)                 return false;
        return true;
    }

    /// Assert-variant for debug builds.
    void assertValid() const noexcept {
        assert(maxDeltaTimeSeconds > 0.0f          && "maxDeltaTimeSeconds must be > 0");
        assert((logicFpsMode  != FrameRateMode::FixedHz || logicHz  > 0) && "logicHz must be > 0 in FixedHz mode");
        assert((renderFpsMode != FrameRateMode::FixedHz || renderHz > 0) && "renderHz must be > 0 in FixedHz mode");
        assert((multithreaded || renderFpsMode != FrameRateMode::Vsync)  && "Vsync render mode requires multithreaded = true");
    }
};

} // namespace spt
