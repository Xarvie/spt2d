#pragma once

#include <atomic>
#include <type_traits>

namespace spt {

template<typename T>
class DoubleBuffer {
public:
    DoubleBuffer() : m_activeIndex(0) {}
    DoubleBuffer(const DoubleBuffer&) = delete;
    DoubleBuffer& operator=(const DoubleBuffer&) = delete;

    T* getWriteBuffer() {
        return &m_buffers[1 - m_activeIndex.load(std::memory_order_acquire)];
    }

    void publish() {
        int current = m_activeIndex.load(std::memory_order_acquire);
        m_activeIndex.store(1 - current, std::memory_order_release);
    }

    const T* getReadBuffer() const {
        return &m_buffers[m_activeIndex.load(std::memory_order_acquire)];
    }

    T* getReadBuffer() {
        return &m_buffers[m_activeIndex.load(std::memory_order_acquire)];
    }

    T& writeBuffer() {
        return m_buffers[1 - m_activeIndex.load(std::memory_order_acquire)];
    }

    const T& readBuffer() const {
        return m_buffers[m_activeIndex.load(std::memory_order_acquire)];
    }

    T& readBuffer() {
        return m_buffers[m_activeIndex.load(std::memory_order_acquire)];
    }

    void swap() {
        int current = m_activeIndex.load(std::memory_order_acquire);
        m_activeIndex.store(1 - current, std::memory_order_release);
    }

private:
    T m_buffers[2];
    std::atomic<int> m_activeIndex;
};

}
