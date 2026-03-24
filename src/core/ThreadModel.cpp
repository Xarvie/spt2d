#include "ThreadModel.h"

#include <thread>
#include <chrono>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <iostream>
#include <cassert>

// ---------------------------------------------------------------------------
// Platform thread-naming and CPU-affinity helpers
// ---------------------------------------------------------------------------
#if defined(__APPLE__)
#  include <pthread.h>
#elif defined(__linux__)
#  include <pthread.h>
#  include <sched.h>
#elif defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif

namespace spt3d {

namespace {

using Clock     = std::chrono::steady_clock;
using TimePoint = Clock::time_point;
using Micros    = std::chrono::microseconds;

// Set the calling thread's OS-visible name (best-effort, never throws).
void setThreadName(const std::string& name) noexcept {
#if defined(__APPLE__)
    pthread_setname_np(name.c_str());
#elif defined(__linux__)
    // Linux enforces a 15-character limit (plus null terminator).
    const std::string clamped = name.size() <= 15 ? name : name.substr(0, 15);
    pthread_setname_np(pthread_self(), clamped.c_str());
#elif defined(_WIN32)
    using SetThreadDescriptionFn = HRESULT(WINAPI*)(HANDLE, PCWSTR);
    static const auto fn = reinterpret_cast<SetThreadDescriptionFn>(
        GetProcAddress(GetModuleHandleW(L"KernelBase.dll"),
                       "SetThreadDescription"));
    if (fn) {
        std::wstring wname(name.begin(), name.end());
        fn(GetCurrentThread(), wname.c_str());
    }
#endif
}

// Set the calling thread's CPU-core affinity (best-effort, never throws).
// mask == 0 means "no preference" → skip silently.
void setThreadAffinity([[maybe_unused]] uint64_t mask) noexcept {
    if (mask == 0) return;
#if defined(__linux__)
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    for (int i = 0; i < 64; ++i) {
        if (mask & (uint64_t(1) << i)) CPU_SET(i, &cpuset);
    }
    pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);
#elif defined(_WIN32)
    SetThreadAffinityMask(GetCurrentThread(),
                          static_cast<DWORD_PTR>(mask));
#endif
}

// ---------------------------------------------------------------------------
//  AtomicFrameStats
//
//  FrameStats is larger than any platform's native atomic width, so we
//  protect it with a small mutex.  Stats are written once per render frame
//  and read only by diagnostic code, so contention is negligible.
// ---------------------------------------------------------------------------
struct AtomicFrameStats {
    void store(const FrameStats& s) noexcept {
        std::lock_guard<std::mutex> lk(mtx);
        val = s;
    }
    [[nodiscard]] FrameStats load() const noexcept {
        std::lock_guard<std::mutex> lk(mtx);
        return val;
    }
    mutable std::mutex mtx;
    FrameStats         val{};
};

} // anonymous namespace


// =============================================================================
//  SingleThreadModel
//
//  Logic and rendering run sequentially on the calling (main/render) thread.
//  No background thread is created.  Useful for debugging, deterministic
//  replay, and platforms that disallow background threads.
//
//  Frame flow:
//    onFrameBegin()   → measure wall-clock dt
//    getRenderWork()  → drain input, onUpdate, onRender → return &work
//    (caller renders)
//    onFrameEnd()     → no-op (work reset at start of next getRenderWork)
// =============================================================================
class SingleThreadModel final : public ThreadModel {
public:
    // -------------------------------------------------------------------------
    bool initialize(const ThreadConfig&         config,
                    std::unique_ptr<IGameLogic>  logic) override {
        assert(logic  && "IGameLogic must not be null");
        config.assertValid();

        m_config = config;
        m_logic  = std::move(logic);

        if (!m_logic->onInit()) {
            std::cerr << "[SingleThreadModel] IGameLogic::onInit() failed\n";
            return false;
        }

        m_initialized = true;
        std::cout << "[SingleThreadModel] Initialized ("
                  << m_config.renderHz << " Hz)\n";
        return true;
    }

    void shutdown() override {
        if (!m_initialized || m_shutdownCalled) return;
        m_shutdownCalled = true;
        if (m_logic) {
            m_logic->onShutdown();
            m_logic.reset();
        }
        std::cout << "[SingleThreadModel] Shut down\n";
    }

    // -------------------------------------------------------------------------
    // Input  –  push* are thread-safe (backed by ThreadSafeQueue)
    // -------------------------------------------------------------------------
    void pushTouchEvent(const TouchEvent& e) override { m_touchQueue.push(e); }
    void pushKeyEvent  (const KeyEvent&   e) override { m_keyQueue.push(e);   }
    void pushMouseEvent(const MouseEvent& e) override { m_mouseQueue.push(e); }

    // -------------------------------------------------------------------------
    // Frame pump
    // -------------------------------------------------------------------------
    void onFrameBegin() override {
        const TimePoint now = Clock::now();
        if (m_lastFrameTime != TimePoint{}) {
            const float raw = std::chrono::duration<float>(
                                  now - m_lastFrameTime).count();
            m_frameDt = m_config.clampDelta(raw);
        }
        m_lastFrameTime = now;
    }

    const GameWork* getRenderWork() override {
        if (!m_logic || !m_logic->isRunning()) return nullptr;

        // Drain all pending input into reusable vectors.
        // drainTo() is O(1) (pointer swap) and retains vector capacity,
        // so there are zero heap allocations in steady state.
        m_touchQueue.drainTo(m_touches);
        m_keyQueue.drainTo(m_keys);
        m_mouseQueue.drainTo(m_mice);

        m_logicTime  += static_cast<double>(m_frameDt);
        m_frameIndex += 1;

        InputFrame input;
        input.touches = &m_touches;
        input.keys    = &m_keys;
        input.mice    = &m_mice;

        m_logic->onUpdate(m_frameDt, input);

        // Reset the work buffer (retains vector/allocator capacity).
        m_work.reset();
        m_work.logicTime  = m_logicTime;
        m_work.deltaTime  = m_frameDt;
        m_work.frameIndex = m_frameIndex;

        m_logic->onRender(m_work);

        // Snapshot diagnostics.
        const auto ws = m_work.stats();
        m_stats.store(FrameStats{
            m_frameIndex,
            m_frameDt,
            m_frameDt,
            m_logicTime,
            static_cast<uint32_t>(ws.renderCommandCount),
            static_cast<uint32_t>(ws.uniformBytesUsed)
        });

        return &m_work;
    }

    void onFrameEnd() override {
        // The work buffer is reset at the beginning of the next
        // getRenderWork(), so the pointer returned this frame remains valid
        // through the caller's render pass.  Nothing to do here.
    }

    // -------------------------------------------------------------------------
    bool isRunning() const override {
        return !m_shutdownCalled && m_logic && m_logic->isRunning();
    }
    bool          isMultithreaded() const override { return false; }
    FrameStats    frameStats()      const override { return m_stats.load(); }
    const ThreadConfig& config()    const override { return m_config; }

private:
    ThreadConfig                m_config;
    std::unique_ptr<IGameLogic> m_logic;

    // Thread-safe input queues (producer may be any thread).
    ThreadSafeQueue<TouchEvent> m_touchQueue;
    ThreadSafeQueue<KeyEvent>   m_keyQueue;
    ThreadSafeQueue<MouseEvent> m_mouseQueue;

    // Reusable drain targets – capacity grows to high-water mark then stays.
    std::vector<TouchEvent> m_touches;
    std::vector<KeyEvent>   m_keys;
    std::vector<MouseEvent> m_mice;

    GameWork         m_work;
    AtomicFrameStats m_stats;

    TimePoint  m_lastFrameTime{};
    float      m_frameDt    = 0.0f;
    double     m_logicTime  = 0.0;
    uint64_t   m_frameIndex = 0;

    bool m_initialized    = false;
    bool m_shutdownCalled = false;
};


// =============================================================================
//  MultiThreadModel
//
//  Logic runs on a dedicated background thread at a fixed rate (logicHz).
//  The render thread fetches the latest completed GameWork snapshot from a
//  TripleBuffer; neither thread ever blocks waiting for the other.
//
//  THE key fix over the original implementation:
//    DoubleBuffer → TripleBuffer.
//
//    With DoubleBuffer, if the logic thread runs faster than the render thread
//    it will wrap around and write into the slot the render thread is currently
//    reading, causing a data race (UB).  TripleBuffer maintains three slots
//    (WRITE / BACK / READ) that are never aliased, eliminating the race
//    unconditionally.
//
//  Logic thread frame flow:
//    drain input queues
//    → onUpdate(fixedDt, input)
//    → workBuffer.writeBuffer().reset()
//    → onRender(writeBuffer)
//    → workBuffer.publish()
//    → sleep until nextTick (or wake early on input / shutdown)
//
//  Render thread frame flow:
//    onFrameBegin()        → measure render dt
//    getRenderWork()       → tripleBuffer.fetch() + return readBuffer ptr
//    (caller issues draw calls using work.renderCommands)
//    onFrameEnd()          → no-op
// =============================================================================
class MultiThreadModel final : public ThreadModel {
public:
    // -------------------------------------------------------------------------
    bool initialize(const ThreadConfig&         config,
                    std::unique_ptr<IGameLogic>  logic) override {
        assert(logic && "IGameLogic must not be null");
        config.assertValid();

        m_config = config;
        m_logic  = std::move(logic);

        if (!m_logic->onInit()) {
            std::cerr << "[MultiThreadModel] IGameLogic::onInit() failed\n";
            return false;
        }

        m_running.store(true, std::memory_order_release);
        m_logicThread = std::thread(&MultiThreadModel::logicLoop, this);

        std::cout << "[MultiThreadModel] Initialized"
                  << " (logic: "  << m_config.logicHz  << " Hz"
                  << ", render: " << m_config.renderHz << " Hz)\n";
        return true;
    }

    // Idempotent: safe to call more than once.
    void shutdown() override {
        if (m_shutdownCalled.exchange(true)) return;

        m_running.store(false, std::memory_order_release);
        wakeLogicThread();

        if (m_logicThread.joinable()) m_logicThread.join();
        // IGameLogic::onShutdown() is called at the end of logicLoop().
    }

    // -------------------------------------------------------------------------
    // Input  –  safe from any thread
    // -------------------------------------------------------------------------
    void pushTouchEvent(const TouchEvent& e) override {
        m_touchQueue.push(e);
        wakeLogicThread();
    }
    void pushKeyEvent(const KeyEvent& e) override {
        m_keyQueue.push(e);
        wakeLogicThread();
    }
    void pushMouseEvent(const MouseEvent& e) override {
        m_mouseQueue.push(e);
        wakeLogicThread();
    }

    // -------------------------------------------------------------------------
    // Frame pump  (render / main thread only)
    // -------------------------------------------------------------------------
    void onFrameBegin() override {
        const TimePoint now = Clock::now();
        if (m_lastRenderTime != TimePoint{}) {
            m_renderDt = m_config.clampDelta(
                std::chrono::duration<float>(now - m_lastRenderTime).count());
        }
        m_lastRenderTime = now;
    }

    const GameWork* getRenderWork() override {
        // Non-blocking: if a new logic frame has been published, swap it into
        // the read slot.  Otherwise keep the previous snapshot – the render
        // thread simply re-renders the last known state, which is correct.
        const bool newFrame = m_workBuffer.fetch();
        const GameWork& work = m_workBuffer.readBuffer();

        if (newFrame) {
            const auto ws = work.stats();
            m_stats.store(FrameStats{
                work.frameIndex,
                work.deltaTime,
                m_renderDt,
                work.logicTime,
                static_cast<uint32_t>(ws.renderCommandCount),
                static_cast<uint32_t>(ws.uniformBytesUsed)
            });
        }

        return &work;
    }

    void onFrameEnd() override {
        // TripleBuffer handles slot ownership autonomously; nothing to do.
    }

    // -------------------------------------------------------------------------
    bool isRunning() const override {
        return m_running.load(std::memory_order_acquire)
            && !m_shutdownCalled.load(std::memory_order_acquire);
    }
    bool          isMultithreaded() const override { return true; }
    FrameStats    frameStats()      const override { return m_stats.load(); }
    const ThreadConfig& config()    const override { return m_config; }

private:
    // -------------------------------------------------------------------------
    // Logic thread
    // -------------------------------------------------------------------------
    void logicLoop() {
        setThreadName(m_config.logicThreadName);
        setThreadAffinity(m_config.logicCpuAffinity);

        std::cout << "[LogicThread] Starting (" << m_config.logicHz << " Hz)\n";

        const float  fixedDt   = m_config.logicDeltaTime();
        const Micros interval  = (m_config.logicHz > 0)
                                  ? Micros(1'000'000LL / m_config.logicHz)
                                  : Micros(0);
        // If we fall more than 200 ms behind, snap forward rather than
        // issuing a burst of catch-up ticks ("spiral of death" prevention).
        const Micros maxLag = std::chrono::milliseconds(200);

        TimePoint  nextTick  = Clock::now();
        double     logicTime = 0.0;
        uint64_t   frameIdx  = 0;

        // Per-thread reusable input vectors – capacity stabilises quickly.
        std::vector<TouchEvent> touches;
        std::vector<KeyEvent>   keys;
        std::vector<MouseEvent> mice;

        while (m_running.load(std::memory_order_acquire)) {

            const TimePoint now = Clock::now();

            // ------------------------------------------------------------------
            // Drain input queues.
            // drainTo() acquires the queue's mutex for a single O(1) pointer
            // swap, then releases it immediately.  The logic thread processes
            // the events without holding any lock.
            // ------------------------------------------------------------------
            m_touchQueue.drainTo(touches);
            m_keyQueue.drainTo(keys);
            m_mouseQueue.drainTo(mice);

            // ------------------------------------------------------------------
            // Fixed-rate logic tick
            // ------------------------------------------------------------------
            if (now >= nextTick) {
                nextTick += interval;

                // Spiral-of-death guard.
                if (interval.count() > 0 && Clock::now() > nextTick + maxLag) {
                    nextTick = Clock::now() + interval;
                }

                logicTime += static_cast<double>(fixedDt);
                ++frameIdx;

                InputFrame input;
                input.touches = &touches;
                input.keys    = &keys;
                input.mice    = &mice;

                m_logic->onUpdate(fixedDt, input);

                // Clear after update so events are not re-delivered next tick.
                touches.clear();
                keys.clear();
                mice.clear();

                // Write into the TripleBuffer's exclusive write slot.
                // This slot is owned solely by the logic thread; no locking
                // is required here.
                GameWork& buf = m_workBuffer.writeBuffer();
                buf.reset();
                buf.logicTime  = logicTime;
                buf.deltaTime  = fixedDt;
                buf.frameIndex = frameIdx;

                m_logic->onRender(buf);

                // Atomically hand the write slot to the render thread as the
                // new "back" (latest) snapshot.  The old back slot is returned
                // to us as the next write target.
                m_workBuffer.publish();

                if (!m_logic->isRunning()) {
                    m_running.store(false, std::memory_order_release);
                }
            }

            // ------------------------------------------------------------------
            // Sleep until the next scheduled tick.
            // The condition variable allows early wake-up on:
            //   a) new input arriving (so events are processed promptly)
            //   b) shutdown() being called
            // ------------------------------------------------------------------
            {
                const TimePoint wakeDeadline = nextTick;
                std::unique_lock<std::mutex> lk(m_sleepMutex);
                m_sleepCV.wait_until(lk, wakeDeadline, [this] {
                    return m_wakeFlag.load(std::memory_order_acquire)
                        || !m_running.load(std::memory_order_acquire);
                });
                m_wakeFlag.store(false, std::memory_order_release);
            }
        }

        // Guaranteed shutdown hook – always reached, even on self-stop.
        if (m_logic) {
            m_logic->onShutdown();
            m_logic.reset();
        }

        std::cout << "[LogicThread] Stopped\n";
    }

    void wakeLogicThread() {
        m_wakeFlag.store(true, std::memory_order_release);
        m_sleepCV.notify_one();
    }

    // -------------------------------------------------------------------------
    // Data members
    // -------------------------------------------------------------------------
    ThreadConfig                m_config;
    std::unique_ptr<IGameLogic> m_logic;

    std::atomic<bool>  m_running{false};
    std::atomic<bool>  m_shutdownCalled{false};
    std::thread        m_logicThread;

    // Logic thread sleep / early-wake mechanism.
    std::mutex              m_sleepMutex;
    std::condition_variable m_sleepCV;
    std::atomic<bool>       m_wakeFlag{false};

    // Input queues.  Multi-producer (any thread), single-consumer (logic thread).
    ThreadSafeQueue<TouchEvent> m_touchQueue;
    ThreadSafeQueue<KeyEvent>   m_keyQueue;
    ThreadSafeQueue<MouseEvent> m_mouseQueue;

    // *** Core fix: TripleBuffer replaces DoubleBuffer ***
    // Producer : logic thread   (writeBuffer() / publish())
    // Consumer : render thread  (fetch() / readBuffer())
    // The three slots are never aliased, so neither thread ever races with
    // the other regardless of their relative speeds.
    TripleBuffer<GameWork> m_workBuffer;

    // Render-thread timing.
    TimePoint  m_lastRenderTime{};
    float      m_renderDt = 0.0f;

    // Frame statistics (written render-side, read from any thread).
    AtomicFrameStats m_stats;
};


// =============================================================================
//  Factory
// =============================================================================
std::unique_ptr<ThreadModel> ThreadModel::create(const ThreadConfig& config) {
    config.assertValid();
    return config.multithreaded
           ? std::unique_ptr<ThreadModel>(std::make_unique<MultiThreadModel>())
           : std::unique_ptr<ThreadModel>(std::make_unique<SingleThreadModel>());
}

} // namespace spt3d
