#pragma once

#include <atomic>
#include <cassert>
#include <cstdint>
#include <type_traits>

namespace spt3d {

// =============================================================================
//  DoubleBuffer<T>
//
//  DEPRECATED — Use TripleBuffer<T> instead for concurrent producer/consumer.
//
//  A simple ping-pong buffer for LOCK-STEP producer/consumer patterns where
//  exactly one side runs at a time (e.g. single-threaded loop, or an explicit
//  synchronisation barrier between the two sides).
//
//  !! DO NOT use this class when producer and consumer run concurrently and
//     independently on separate threads. Use TripleBuffer<T> instead. !!
//
//  Thread-safety model:
//    - writeBuffer() / publish() : called exclusively by the PRODUCER thread.
//    - readBuffer()              : called exclusively by the CONSUMER thread.
//    - Both sides must be separated by an external synchronisation point
//      (barrier, semaphore, join, …) between each write→read cycle.
// =============================================================================
template <typename T>
class
#if __cplusplus >= 201402L
    [[deprecated("Use TripleBuffer<T> for concurrent producer/consumer patterns")]]
#endif
DoubleBuffer {
public:
    static_assert(std::is_default_constructible<T>::value,
                  "DoubleBuffer<T>: T must be default-constructible");

    DoubleBuffer()                             = default;
    DoubleBuffer(const DoubleBuffer&)          = delete;
    DoubleBuffer& operator=(const DoubleBuffer&) = delete;

    // -------------------------------------------------------------------------
    // Producer-side (single producer thread only)
    // -------------------------------------------------------------------------

    /// Returns a reference to the buffer the producer should write into.
    T& writeBuffer() noexcept {
        return m_buffers[writeIndex()];
    }

    /// Makes the previously written buffer visible to the consumer.
    /// Call exactly once after finishing a write pass.
    void publish() noexcept {
        const int cur = m_activeIndex.load(std::memory_order_relaxed);
        m_activeIndex.store(1 - cur, std::memory_order_release);
    }

    // -------------------------------------------------------------------------
    // Consumer-side (single consumer thread only)
    // -------------------------------------------------------------------------

    const T& readBuffer() const noexcept {
        return m_buffers[m_activeIndex.load(std::memory_order_acquire)];
    }

    T& readBuffer() noexcept {
        return m_buffers[m_activeIndex.load(std::memory_order_acquire)];
    }

private:
    int writeIndex() const noexcept {
        return 1 - m_activeIndex.load(std::memory_order_relaxed);
    }

    T              m_buffers[2];
    std::atomic<int> m_activeIndex{0};
};


// =============================================================================
//  TripleBuffer<T>
//
//  A lock-free triple buffer designed for fully ASYNCHRONOUS producer/consumer
//  pipelines (e.g. a game-logic thread writing at 30 Hz and a render thread
//  reading at 60 Hz).
//
//  Key properties:
//    - Neither the producer nor the consumer ever blocks or spins waiting for
//      the other.
//    - The consumer always reads the MOST RECENTLY published snapshot; stale
//      intermediate frames written by a faster producer are silently discarded.
//    - Zero dynamic allocation. All three buffers live inside the object.
//
//  Ownership invariant:
//    At every point in time the three buffer slots are partitioned into exactly
//    three distinct roles:
//
//      WRITE  slot  – exclusively owned by the producer thread.
//      BACK   slot  – the latest completed snapshot, shared atomically.
//      READ   slot  – exclusively owned by the consumer thread.
//
//    m_writeSlot and m_readSlot are private to their respective threads and
//    require no atomic operations. Only m_backState is shared, encoded as:
//
//      bit  31     : kNewDataFlag – set by publish(), cleared by fetch()
//      bits [1:0]  : index of the BACK slot (0, 1, or 2)
//
//  Thread-safety model:
//    - writeBuffer() / publish() : called exclusively by the PRODUCER thread.
//    - fetch() / readBuffer()    : called exclusively by the CONSUMER thread.
//    - No external locking required.
// =============================================================================
template <typename T>
class TripleBuffer {
public:
    static_assert(std::is_default_constructible<T>::value,
                  "TripleBuffer<T>: T must be default-constructible");

    /// Initialises the buffer in a clean state: no new data available.
    TripleBuffer() noexcept
        : m_backState(kSlot1)   // slot 1 is the initial "back"; no new-data flag
        , m_writeSlot(kSlot0)   // producer starts writing into slot 0
        , m_readSlot(kSlot2)    // consumer starts reading from slot 2
    {
    }

    TripleBuffer(const TripleBuffer&)            = delete;
    TripleBuffer& operator=(const TripleBuffer&) = delete;

    // -------------------------------------------------------------------------
    // Producer-side API  (call ONLY from the producer / logic thread)
    // -------------------------------------------------------------------------

    /// Returns a reference to the buffer the producer should write into.
    /// The returned reference is valid until the next call to publish().
    T& writeBuffer() noexcept {
        return m_buffers[m_writeSlot];
    }

    /// Atomically publishes the write buffer as the new "back" (latest) snapshot.
    ///
    /// The previous back buffer is handed back to the producer as its next
    /// write target, so intermediate frames are overwritten rather than queued.
    /// This means a slow consumer will always receive the freshest data without
    /// unbounded memory growth.
    void publish() noexcept {
        // Stamp our write-slot index with the new-data flag and atomically
        // swap it into m_backState. The old back slot (whatever the consumer
        // last published or the initial value) comes back to us as the new
        // write slot. The new-data flag on the returned value is irrelevant
        // (the consumer may or may not have cleared it), so we mask it off.
        const uint32_t newBack = static_cast<uint32_t>(m_writeSlot) | kNewDataFlag;
        const uint32_t prev    = m_backState.exchange(newBack, std::memory_order_acq_rel);
        m_writeSlot            = static_cast<int>(prev & kSlotMask);
    }

    // -------------------------------------------------------------------------
    // Consumer-side API  (call ONLY from the consumer / render thread)
    // -------------------------------------------------------------------------

    /// Attempts to acquire the latest published snapshot.
    ///
    /// If a new frame has been published since the last fetch(), the consumer's
    /// read slot is atomically swapped with the back slot and the function
    /// returns true. Otherwise returns false and leaves the read slot unchanged.
    ///
    /// Typical render-loop usage:
    ///   tripleBuffer.fetch();           // grab latest (or keep last if none)
    ///   const T& data = tripleBuffer.readBuffer();
    bool fetch() noexcept {
        const uint32_t state = m_backState.load(std::memory_order_acquire);
        if (!(state & kNewDataFlag)) {
            return false;   // No new frame since last fetch
        }

        // Swap our (stale) read slot into the back position and take the
        // published back slot as our new read slot. We do NOT set the new-data
        // flag when writing our old read slot back, because that data is not
        // "new" from the producer's perspective.
        const uint32_t mySlot = static_cast<uint32_t>(m_readSlot); // no flag
        const uint32_t prev   = m_backState.exchange(mySlot, std::memory_order_acq_rel);
        m_readSlot            = static_cast<int>(prev & kSlotMask);
        return true;
    }

    /// Returns the current read buffer.
    /// Always valid; contains either the snapshot from the last successful
    /// fetch() or the default-constructed T if fetch() was never called.
    const T& readBuffer() const noexcept {
        return m_buffers[m_readSlot];
    }

    T& readBuffer() noexcept {
        return m_buffers[m_readSlot];
    }

    /// Convenience helper: fetch latest data (if any), then return read buffer.
    /// Equivalent to calling fetch() followed by readBuffer().
    T& syncAndRead() noexcept {
        fetch();
        return readBuffer();
    }

    const T& syncAndRead() const noexcept {
        const_cast<TripleBuffer*>(this)->fetch();
        return readBuffer();
    }

    /// Returns true if a new frame has been published but not yet consumed.
    /// Useful for diagnostics; do not use this as a synchronisation primitive.
    bool hasNewData() const noexcept {
        return (m_backState.load(std::memory_order_acquire) & kNewDataFlag) != 0;
    }

private:
    // Slot indices and bit-field constants.
    static constexpr uint32_t kSlot0       = 0u;
    static constexpr uint32_t kSlot1       = 1u;
    static constexpr uint32_t kSlot2       = 2u;
    static constexpr uint32_t kSlotMask    = 0x03u;
    static constexpr uint32_t kNewDataFlag = 0x8000'0000u;

    // The three data buffers. Default-constructed on startup.
    T m_buffers[3];

    // Shared atomic state: [31]=new-data flag, [1:0]=back slot index.
    // Written by both producer (publish) and consumer (fetch) via exchange().
    std::atomic<uint32_t> m_backState;

    // Private to the producer thread – no atomic access needed.
    int m_writeSlot;

    // Private to the consumer thread – no atomic access needed.
    int m_readSlot;
};

} // namespace spt3d