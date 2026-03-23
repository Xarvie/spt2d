#pragma once

#include <vector>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <cassert>
#include <cstddef>

namespace spt {

// =============================================================================
//  ThreadSafeQueue<T>
//
//  A mutex-protected, multi-producer / single-consumer queue optimised for the
//  common game-engine pattern of accumulating events on any thread and draining
//  all of them at once on a dedicated consumer thread every frame.
//
//  Design decisions vs. the original implementation:
//
//  1. Internal storage is std::vector<T> rather than std::queue<std::deque<T>>.
//     This gives contiguous memory layout (better cache behaviour when iterating
//     the drained batch) and lets the vector retain its capacity across frames,
//     eliminating per-frame heap allocations after the first few frames.
//
//  2. drainTo(std::vector<T>&) swaps the internal pending vector with the
//     caller's output vector under the lock (O(1), no element copying) and then
//     releases the lock immediately. The caller processes the batch lock-free.
//     Because the same output vector is reused every frame its capacity also
//     stabilises quickly, achieving zero allocations in steady state.
//
//  3. A close() / isClosed() mechanism enables clean producer shutdown:
//     producers check isClosed() before pushing; the consumer can drain any
//     remaining items after close() returns.
//
//  4. Blocking variants (waitDrainAll / waitPop) are provided for threads that
//     should sleep rather than spin when the queue is empty.
//
//  Thread-safety model:
//    - push() / pushRange() : safe to call from any number of threads.
//    - drainTo() / waitDrainAll() / tryPop() / waitPop() :
//        intended for a SINGLE consumer thread. Calling drain from multiple
//        threads concurrently is safe but rarely useful and may lose ordering.
//    - close() : safe to call from any thread; idempotent.
// =============================================================================
template <typename T>
class ThreadSafeQueue {
public:
    ThreadSafeQueue()  = default;
    ~ThreadSafeQueue() = default;

    ThreadSafeQueue(const ThreadSafeQueue&)            = delete;
    ThreadSafeQueue& operator=(const ThreadSafeQueue&) = delete;

    // -------------------------------------------------------------------------
    // Producer-side API
    // -------------------------------------------------------------------------

    /// Pushes a single item.  Returns false (and discards the item) if the
    /// queue has been closed.
    bool push(T value) {
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            if (m_closed) return false;
            m_pending.push_back(std::move(value));
        }
        m_cond.notify_one();
        return true;
    }

    /// Pushes all items from [first, last).  The range is consumed in one lock
    /// acquisition, minimising contention when batching events.
    /// Returns the number of items actually enqueued (0 if closed).
    template <typename InputIt>
    std::size_t pushRange(InputIt first, InputIt last) {
        std::size_t count = 0;
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            if (m_closed) return 0;
            for (auto it = first; it != last; ++it, ++count) {
                m_pending.push_back(*it);
            }
        }
        if (count > 0) m_cond.notify_one();
        return count;
    }

    // -------------------------------------------------------------------------
    // Consumer-side API
    // -------------------------------------------------------------------------

    /// Non-blocking drain.  Atomically swaps all pending items into `out`
    /// (which is cleared first), then releases the lock immediately.
    ///
    /// After the call `out` holds every item that was pending at the moment of
    /// the swap; new pushes by producers go into a fresh internal vector.
    ///
    /// Passing the same `out` vector every frame preserves its capacity,
    /// achieving zero heap allocations in steady state.
    ///
    /// Returns the number of items drained (0 if the queue was empty).
    std::size_t drainTo(std::vector<T>& out) {
        out.clear();
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            if (m_pending.empty()) return 0;
            out.swap(m_pending);   // O(1): swap internal pointers
            // m_pending is now the (cleared) vector that `out` used to be;
            // it retains whatever capacity `out` had, ready for next frame.
        }
        return out.size();
    }

    /// Blocking drain.  Waits until at least one item is available (or the
    /// queue is closed), then drains everything into `out`.
    ///
    /// Returns the number of items drained.  Returns 0 only when the queue is
    /// both closed and empty, signalling the consumer it should stop.
    std::size_t waitDrainAll(std::vector<T>& out) {
        out.clear();
        std::unique_lock<std::mutex> lk(m_mutex);
        m_cond.wait(lk, [this] {
            return !m_pending.empty() || m_closed;
        });
        if (m_pending.empty()) return 0;
        out.swap(m_pending);
        return out.size();
    }

    /// Blocking drain with timeout.  Behaves like waitDrainAll() but returns
    /// after `timeout` even if no items arrived.
    template <typename Rep, typename Period>
    std::size_t waitDrainAll(std::vector<T>& out,
                             const std::chrono::duration<Rep, Period>& timeout) {
        out.clear();
        std::unique_lock<std::mutex> lk(m_mutex);
        m_cond.wait_for(lk, timeout, [this] {
            return !m_pending.empty() || m_closed;
        });
        if (m_pending.empty()) return 0;
        out.swap(m_pending);
        return out.size();
    }

    /// Non-blocking single-item pop.  Returns false if the queue is empty.
    bool tryPop(T& out) {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (m_pending.empty()) return false;
        out = std::move(m_pending.front());
        m_pending.erase(m_pending.begin());   // O(n) — prefer drainTo for bulk
        return true;
    }

    /// Blocking single-item pop.  Waits until an item is available or the
    /// queue is closed.  Returns false if the queue is closed and empty.
    bool waitPop(T& out) {
        std::unique_lock<std::mutex> lk(m_mutex);
        m_cond.wait(lk, [this] {
            return !m_pending.empty() || m_closed;
        });
        if (m_pending.empty()) return false;
        out = std::move(m_pending.front());
        m_pending.erase(m_pending.begin());
        return true;
    }

    // -------------------------------------------------------------------------
    // Lifecycle
    // -------------------------------------------------------------------------

    /// Marks the queue as closed.  Subsequent push() calls will be rejected.
    /// Any thread blocked in waitDrainAll() or waitPop() will be woken up so
    /// it can observe the closed state and exit cleanly.
    /// Idempotent: safe to call multiple times.
    void close() {
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            m_closed = true;
        }
        m_cond.notify_all();
    }

    /// Discards all pending items and resets the queue to the open state.
    /// Intended for teardown/re-initialisation; do not call while producers
    /// or consumers are actively using the queue.
    void reset() {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_pending.clear();
        m_closed = false;
    }

    // -------------------------------------------------------------------------
    // Observers (approximate — state may change immediately after the call)
    // -------------------------------------------------------------------------

    [[nodiscard]] bool empty() const {
        std::lock_guard<std::mutex> lk(m_mutex);
        return m_pending.empty();
    }

    [[nodiscard]] std::size_t size() const {
        std::lock_guard<std::mutex> lk(m_mutex);
        return m_pending.size();
    }

    [[nodiscard]] bool isClosed() const {
        std::lock_guard<std::mutex> lk(m_mutex);
        return m_closed;
    }

private:
    mutable std::mutex      m_mutex;
    std::condition_variable m_cond;
    std::vector<T>          m_pending;
    bool                    m_closed{false};
};

} // namespace spt
