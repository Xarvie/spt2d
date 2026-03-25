// spt3d/core/ThreadModel.h — Thread model abstraction
// [THREAD: mixed] — See per-method documentation.
//
// Two implementations:
//   SingleThreadModel  — logic + render sequential on main thread.
//   MultiThreadModel   — logic on background thread, render on main thread.
//                        Connected by TripleBuffer<GameWork>.
//
// Render-thread frame pump:
//   while (model->isRunning()) {
//       model->onFrameBegin();
//       const GameWork* work = model->getRenderWork();  // const!
//       if (work) executor.execute(*work, gpu);
//       model->onFrameEnd();
//       swapBuffers();
//   }
#pragma once

#include "ThreadConfig.h"
#include "ThreadSafeQueue.h"
#include "GameWork.h"
#include "../Types.h"

#include <memory>
#include <vector>
#include <cstdint>

namespace spt3d {

// =========================================================================
//  InputFrame — snapshot of input events for a single logic tick
// =========================================================================

struct InputFrame {
    const std::vector<TouchEvent>* touches = nullptr;
    const std::vector<KeyEvent>*   keys    = nullptr;
    const std::vector<MouseEvent>* mice    = nullptr;

    const std::vector<TouchEvent>& touchEvents() const noexcept {
        static const std::vector<TouchEvent> e; return touches ? *touches : e;
    }
    const std::vector<KeyEvent>& keyEvents() const noexcept {
        static const std::vector<KeyEvent> e; return keys ? *keys : e;
    }
    const std::vector<MouseEvent>& mouseEvents() const noexcept {
        static const std::vector<MouseEvent> e; return mice ? *mice : e;
    }
};

// =========================================================================
//  FrameStats — per-frame diagnostics
// =========================================================================

struct FrameStats {
    uint64_t frameIndex         = 0;
    float    logicDeltaTime     = 0.0f;
    float    renderDeltaTime    = 0.0f;
    double   logicTime          = 0.0;
    uint32_t renderCommandCount = 0;
    uint32_t uniformBytesUsed   = 0;
};

// =========================================================================
//  IGameLogic — user-implemented game logic interface
// =========================================================================
//
// In multi-threaded mode, all methods run on the logic thread.
// In single-threaded mode, all methods run on the main thread.
//
// CRITICAL: onRender() must NOT call any GL API. It only fills GameWork
// with commands, handles, and pool-allocated data. The render thread
// executes those commands later.

class IGameLogic {
public:
    virtual ~IGameLogic() = default;

    /// Called once before the first frame. Return false to abort.
    virtual bool onInit() = 0;

    /// Called every logic tick. dt is clamped to ThreadConfig::maxDeltaTimeSeconds.
    virtual void onUpdate(float dt, const InputFrame& input) = 0;

    /// Fill `work` with render commands for this frame.
    /// work.reset() has already been called. Do NOT call any GL functions here.
    virtual void onRender(GameWork& work) = 0;

    /// Called once after the last frame (even on self-stop).
    virtual void onShutdown() = 0;

    /// Return false to request clean shutdown.
    virtual bool isRunning() const = 0;
};

// =========================================================================
//  ThreadModel — abstract base
// =========================================================================

class ThreadModel {
public:
    virtual ~ThreadModel() = default;

    // ── Lifecycle ───────────────────────────────────────────────────────
    virtual bool initialize(const ThreadConfig& config,
                            std::unique_ptr<IGameLogic> logic) = 0;
    virtual void shutdown() = 0;

    // ── Input (thread-safe, call from any thread) ───────────────────────
    virtual void pushTouchEvent(const TouchEvent& e) = 0;
    virtual void pushKeyEvent  (const KeyEvent&   e) = 0;
    virtual void pushMouseEvent(const MouseEvent& e) = 0;

    // ── Render-thread frame pump ────────────────────────────────────────
    virtual void             onFrameBegin()  = 0;
    virtual const GameWork*  getRenderWork() = 0;   // returns CONST pointer
    virtual void             onFrameEnd()    = 0;

    // ── State ───────────────────────────────────────────────────────────
    virtual bool              isRunning()       const = 0;
    virtual bool              isMultithreaded() const = 0;
    virtual FrameStats        frameStats()      const = 0;
    virtual const ThreadConfig& config()        const = 0;

    // ── Factory ─────────────────────────────────────────────────────────
    static std::unique_ptr<ThreadModel> create(const ThreadConfig& config);
};

} // namespace spt3d
