#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>

namespace spt {

template<typename T>
class ThreadSafeQueue {
public:
    void push(T value) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.push(std::move(value));
        m_cond.notify_one();
    }

    void pushAll(std::queue<T>& items) {
        if (items.empty()) return;
        std::lock_guard<std::mutex> lock(m_mutex);
        while (!items.empty()) {
            m_queue.push(std::move(items.front()));
            items.pop();
        }
        m_cond.notify_one();
    }

    bool tryPop(T& outValue) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_queue.empty()) return false;
        outValue = std::move(m_queue.front());
        m_queue.pop();
        return true;
    }

    void tryPopAll(std::queue<T>& outQueue) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_queue.empty()) return;
        outQueue.swap(m_queue);
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_queue.empty();
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_queue.size();
    }

    void clear() {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::queue<T> empty;
        m_queue.swap(empty);
    }

private:
    mutable std::mutex m_mutex;
    std::queue<T> m_queue;
    std::condition_variable m_cond;
};

}
