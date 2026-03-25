// spt3d/core/TripleBuffer.h — Lock-free triple buffer
// [THREAD: producer+consumer] — See ownership invariant below.
//
// Designed for fully ASYNCHRONOUS producer/consumer pipelines.
// Neither side ever blocks. Consumer always reads the MOST RECENT snapshot.
// Zero dynamic allocation — all three buffers live inside the object.
//
// Ownership invariant:
//   WRITE slot — exclusively owned by the producer thread.
//   BACK  slot — latest completed snapshot (shared atomically via index).
//   READ  slot — exclusively owned by the consumer thread.
//
// Thread-safety:
//   writeBuffer() / publish()      : producer thread only.
//   fetch() / readBuffer()         : consumer thread only.
//   No external locking required.
#pragma once

#include <atomic>
#include <cstdint>
#include <type_traits>

namespace spt3d {

template <typename T>
class TripleBuffer {
public:
    static_assert(std::is_default_constructible<T>::value,
                  "TripleBuffer<T>: T must be default-constructible");

    TripleBuffer() noexcept
        : m_backState(kSlot1)
        , m_writeSlot(kSlot0)
        , m_readSlot(kSlot2)
    {}

    TripleBuffer(const TripleBuffer&)            = delete;
    TripleBuffer& operator=(const TripleBuffer&) = delete;

    // ── Producer API ────────────────────────────────────────────────────

    T& writeBuffer() noexcept { return m_buffers[m_writeSlot]; }

    void publish() noexcept {
        const uint32_t newBack = static_cast<uint32_t>(m_writeSlot) | kNewDataFlag;
        const uint32_t prev    = m_backState.exchange(newBack, std::memory_order_acq_rel);
        m_writeSlot            = static_cast<int>(prev & kSlotMask);
    }

    // ── Consumer API ────────────────────────────────────────────────────

    bool fetch() noexcept {
        const uint32_t state = m_backState.load(std::memory_order_acquire);
        if (!(state & kNewDataFlag)) return false;
        const uint32_t mySlot = static_cast<uint32_t>(m_readSlot);
        const uint32_t prev   = m_backState.exchange(mySlot, std::memory_order_acq_rel);
        m_readSlot            = static_cast<int>(prev & kSlotMask);
        return true;
    }

    const T& readBuffer() const noexcept { return m_buffers[m_readSlot]; }
    T&       readBuffer()       noexcept { return m_buffers[m_readSlot]; }

    const T& syncAndRead() noexcept { fetch(); return readBuffer(); }

    bool hasNewData() const noexcept {
        return (m_backState.load(std::memory_order_acquire) & kNewDataFlag) != 0;
    }

private:
    static constexpr uint32_t kSlot0       = 0u;
    static constexpr uint32_t kSlot1       = 1u;
    static constexpr uint32_t kSlot2       = 2u;
    static constexpr uint32_t kSlotMask    = 0x03u;
    static constexpr uint32_t kNewDataFlag = 0x8000'0000u;

    T                     m_buffers[3];
    std::atomic<uint32_t> m_backState;
    int                   m_writeSlot;
    int                   m_readSlot;
};

} // namespace spt3d
