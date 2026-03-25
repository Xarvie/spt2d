// spt3d/core/ThreadSafeQueue.h — Multi-producer / single-consumer queue
// [THREAD: any→push, consumer→drain]
//
// drainTo() is O(1) pointer swap. Same output vector reused every frame
// → zero heap alloc in steady state. close() enables clean shutdown.
#pragma once

#include <vector>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <cstddef>

namespace spt3d {

template <typename T>
class ThreadSafeQueue {
public:
    ThreadSafeQueue()  = default;
    ~ThreadSafeQueue() = default;
    ThreadSafeQueue(const ThreadSafeQueue&)            = delete;
    ThreadSafeQueue& operator=(const ThreadSafeQueue&) = delete;

    bool push(T value) {
        { std::lock_guard<std::mutex> lk(m_mutex);
          if (m_closed) return false;
          m_pending.push_back(std::move(value)); }
        m_cond.notify_one();
        return true;
    }

    template <typename It>
    std::size_t pushRange(It first, It last) {
        std::size_t n = 0;
        { std::lock_guard<std::mutex> lk(m_mutex);
          if (m_closed) return 0;
          for (auto it = first; it != last; ++it, ++n) m_pending.push_back(*it); }
        if (n) m_cond.notify_one();
        return n;
    }

    std::size_t drainTo(std::vector<T>& out) {
        out.clear();
        { std::lock_guard<std::mutex> lk(m_mutex);
          if (m_pending.empty()) return 0;
          out.swap(m_pending); }
        return out.size();
    }

    std::size_t waitDrainAll(std::vector<T>& out) {
        out.clear();
        std::unique_lock<std::mutex> lk(m_mutex);
        m_cond.wait(lk, [this]{ return !m_pending.empty() || m_closed; });
        if (m_pending.empty()) return 0;
        out.swap(m_pending);
        return out.size();
    }

    template <typename Rep, typename Period>
    std::size_t waitDrainAll(std::vector<T>& out, const std::chrono::duration<Rep,Period>& t) {
        out.clear();
        std::unique_lock<std::mutex> lk(m_mutex);
        m_cond.wait_for(lk, t, [this]{ return !m_pending.empty() || m_closed; });
        if (m_pending.empty()) return 0;
        out.swap(m_pending);
        return out.size();
    }

    bool tryPop(T& out) {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (m_pending.empty()) return false;
        out = std::move(m_pending.front());
        m_pending.erase(m_pending.begin());
        return true;
    }

    void close() {
        { std::lock_guard<std::mutex> lk(m_mutex); m_closed = true; }
        m_cond.notify_all();
    }

    void reset() {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_pending.clear(); m_closed = false;
    }

    [[nodiscard]] bool        empty()    const { std::lock_guard<std::mutex> lk(m_mutex); return m_pending.empty(); }
    [[nodiscard]] std::size_t size()     const { std::lock_guard<std::mutex> lk(m_mutex); return m_pending.size(); }
    [[nodiscard]] bool        isClosed() const { std::lock_guard<std::mutex> lk(m_mutex); return m_closed; }

private:
    mutable std::mutex      m_mutex;
    std::condition_variable m_cond;
    std::vector<T>          m_pending;
    bool                    m_closed{false};
};

} // namespace spt3d
