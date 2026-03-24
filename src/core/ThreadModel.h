#pragma once

#include "ThreadConfig.h"
#include "ThreadSafeQueue.h"
#include "DoubleBuffer.h"
#include "GameWork.h"
#include "../platform/Platform.h"

#include <functional>
#include <vector>
#include <memory>
#include <cstdint>

namespace spt3d {

// =============================================================================
//  InputFrame
//
//  A snapshot of all input events accumulated since the last logic tick.
//  Passed by const-ref to IGameLogic::onUpdate() so the logic layer never
//  needs to interact with the thread-safe queues directly.
//
//  All vectors are owned by the ThreadModel implementation and are valid only
//  for the duration of the onUpdate() call.  Do not store pointers into them.
// =============================================================================
struct InputFrame {
    const std::vector<TouchEvent>* touches = nullptr;
    const std::vector<KeyEvent>*   keys    = nullptr;
    const std::vector<MouseEvent>* mice    = nullptr;

    // Convenience accessors – never return nullptr.
    const std::vector<TouchEvent>& touchEvents() const noexcept {
        static const std::vector<TouchEvent> empty;
        return touches ? *touches : empty;
    }
    const std::vector<KeyEvent>& keyEvents() const noexcept {
        static const std::vector<KeyEvent> empty;
        return keys ? *keys : empty;
    }
    const std::vector<MouseEvent>& mouseEvents() const noexcept {
        static const std::vector<MouseEvent> empty;
        return mice ? *mice : empty;
    }
};


// =============================================================================
//  FrameStats
//
//  Per-frame diagnostic data exposed by ThreadModel::frameStats().
//  All values are filled in on the render thread; read them after
//  getRenderWork() returns and before onFrameEnd().
// =============================================================================
struct FrameStats {
    uint64_t frameIndex        = 0;
    float    logicDeltaTime    = 0.0f;   ///< dt used by the last logic tick
    float    renderDeltaTime   = 0.0f;   ///< wall-clock time of the last render frame
    double   logicTime         = 0.0;    ///< accumulated simulation time
    uint32_t renderCommandCount = 0;
    uint32_t uniformBytesUsed   = 0;
};


// =============================================================================
//  IGameLogic
//
//  The interface the application implements to drive its simulation and
//  produce render commands.
//
//  Lifecycle contract (guaranteed by ThreadModel):
//
//    onInit()
//      Called once before the first frame, on whichever thread the logic runs.
//      Return false to abort initialisation; ThreadModel::initialize() will
//      propagate the failure to the caller.
//
//    onUpdate(dt, input)
//      Called every logic tick (fixed-step in MultiThreadModel, variable in
//      SingleThreadModel).  `dt` has already been clamped to
//      ThreadConfig::maxDeltaTimeSeconds.
//
//    onRender(work)
//      Called immediately after onUpdate() on the same thread.  The
//      implementation should populate `work` with RenderCommands, GPU tasks,
//      and platform commands for this frame.  `work` has been reset() before
//      this call.
//
//    onShutdown()
//      Called once after the last frame, guaranteed to be called even if
//      isRunning() returns false (i.e. after a graceful self-stop).
//
//    isRunning()
//      The ThreadModel polls this after each tick.  Return false to request a
//      clean shutdown; onShutdown() will follow.
// =============================================================================
class IGameLogic {
public:
    virtual ~IGameLogic() = default;

    virtual bool onInit()                                    = 0;
    virtual void onUpdate(float dt, const InputFrame& input) = 0;
    virtual void onRender(GameWork& work)                    = 0;
    virtual void onShutdown()                                = 0;
    virtual bool isRunning() const                           = 0;
};


// =============================================================================
//  ThreadModel
//
//  Abstract base for the two execution strategies (single-threaded and
//  multi-threaded).  The render / main thread drives ThreadModel via this
//  interface each frame:
//
//    while (model->isRunning()) {
//        model->onFrameBegin();
//
//        const GameWork* work = model->getRenderWork();
//        if (work) {
//            renderer.execute(*work);          // draw
//        }
//
//        model->onFrameEnd();
//        swapBuffers();
//    }
//
//  Input events may be pushed from any thread at any time between
//  initialize() and shutdown().
//
//  Create via the factory method ThreadModel::create(config).
// =============================================================================
class ThreadModel {
public:
    virtual ~ThreadModel() = default;

    // -------------------------------------------------------------------------
    // Lifecycle
    // -------------------------------------------------------------------------

    /// Initialise the model and start the logic thread (if multithreaded).
    /// `logic` is moved into the model; the caller must not use it afterwards.
    /// Returns false if IGameLogic::onInit() fails or config is invalid.
    virtual bool initialize(const ThreadConfig&          config,
                            std::unique_ptr<IGameLogic>  logic) = 0;

    /// Signal shutdown and block until the logic thread has exited (if any).
    /// IGameLogic::onShutdown() is guaranteed to have been called on return.
    /// Safe to call more than once; subsequent calls are no-ops.
    virtual void shutdown() = 0;

    // -------------------------------------------------------------------------
    // Input  (thread-safe; call from any thread)
    // -------------------------------------------------------------------------

    virtual void pushTouchEvent(const TouchEvent& event) = 0;
    virtual void pushKeyEvent  (const KeyEvent&   event) = 0;
    virtual void pushMouseEvent(const MouseEvent& event) = 0;

    // -------------------------------------------------------------------------
    // Render-thread frame pump
    //
    // Call in order: onFrameBegin → getRenderWork → (render) → onFrameEnd.
    // -------------------------------------------------------------------------

    /// Mark the start of a new render frame.  Updates internal timing.
    virtual void onFrameBegin() = 0;

    /// Returns a pointer to the latest completed GameWork snapshot, or nullptr
    /// if no frame has been produced yet (e.g. first frame in multi-threaded
    /// mode before the logic thread has ticked once).
    ///
    /// The returned pointer is valid until onFrameEnd() is called.
    /// In SingleThreadModel this also drives the logic tick.
    virtual const GameWork* getRenderWork() = 0;

    /// Mark the end of the render frame.  In SingleThreadModel this clears the
    /// GameWork buffer so it is ready for the next tick.
    virtual void onFrameEnd() = 0;

    // -------------------------------------------------------------------------
    // State queries
    // -------------------------------------------------------------------------

    /// Returns true while the logic is running and shutdown() has not been
    /// called.  Safe to call from any thread (result is advisory).
    virtual bool isRunning()      const = 0;
    virtual bool isMultithreaded() const = 0;

    /// Returns a snapshot of per-frame diagnostics populated each render frame.
    virtual FrameStats frameStats() const = 0;

    virtual const ThreadConfig& config() const = 0;

    // -------------------------------------------------------------------------
    // Factory
    // -------------------------------------------------------------------------

    /// Construct the appropriate implementation based on config.multithreaded.
    static std::unique_ptr<ThreadModel> create(const ThreadConfig& config);
};

} // namespace spt3d
