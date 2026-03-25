// spt3d/core/LinearAllocator.h — Frame-scoped bump allocator
// [THREAD: logic] — Written by the logic thread during onRender().
//                    Read by the render thread after publish() (immutable at that point).
//
// A simple bump allocator backed by std::vector<uint8_t>. Provides
// pointer-stable allocations within a frame: the buffer never reallocates
// once it has reached its steady-state capacity, and all allocations are
// freed at once by reset().
//
// Usage:
//   pool.reset();                     // start of frame
//   auto* p = pool.alloc<MyUBO>();    // bump-allocate
//   *p = myData;                      // write
//   uint32_t off = pool.offsetOf(p);  // store offset in RenderCommand
//   // ... later on render thread:
//   auto* q = pool.ptrAt<MyUBO>(off); // recover pointer from offset
#pragma once

#include <vector>
#include <cstdint>
#include <cstddef>
#include <cassert>

namespace spt3d {

class LinearAllocator {
public:
    explicit LinearAllocator(std::size_t initialCapacity = 0) {
        if (initialCapacity > 0) m_buffer.reserve(initialCapacity);
    }

    // Non-copyable but movable (moved as part of GameWork slot swap).
    LinearAllocator(const LinearAllocator&)            = default;
    LinearAllocator& operator=(const LinearAllocator&) = default;
    LinearAllocator(LinearAllocator&&)                 = default;
    LinearAllocator& operator=(LinearAllocator&&)      = default;

    /// Allocate `size` bytes aligned to `alignment`. Returns nullptr if
    /// the allocation would exceed a hard cap (currently unbounded — the
    /// vector grows as needed, amortising cost).
    void* alloc(std::size_t size, std::size_t alignment = alignof(std::max_align_t)) {
        assert(alignment > 0 && (alignment & (alignment - 1)) == 0);
        const std::size_t cur    = m_buffer.size();
        const std::size_t pad    = (alignment - (cur & (alignment - 1))) & (alignment - 1);
        const std::size_t needed = cur + pad + size;
        if (needed > m_buffer.capacity()) {
            m_buffer.reserve(std::max(needed, m_buffer.capacity() * 2));
        }
        m_buffer.resize(needed);
        return m_buffer.data() + cur + pad;
    }

    /// Typed convenience wrapper.
    template <typename T>
    T* alloc() {
        void* p = alloc(sizeof(T), alignof(T));
        return p ? static_cast<T*>(p) : nullptr;
    }

    /// Byte offset of a pointer previously obtained from alloc().
    uint32_t offsetOf(const void* p) const noexcept {
        const auto base = reinterpret_cast<uintptr_t>(m_buffer.data());
        const auto ptr  = reinterpret_cast<uintptr_t>(p);
        assert(ptr >= base && ptr < base + m_buffer.size());
        return static_cast<uint32_t>(ptr - base);
    }

    /// Recover a typed pointer from a stored offset.
    template <typename T>
    T* ptrAt(uint32_t offset) noexcept {
        assert(offset + sizeof(T) <= m_buffer.size());
        return reinterpret_cast<T*>(m_buffer.data() + offset);
    }
    template <typename T>
    const T* ptrAt(uint32_t offset) const noexcept {
        assert(offset + sizeof(T) <= m_buffer.size());
        return reinterpret_cast<const T*>(m_buffer.data() + offset);
    }

    /// Free all allocations without releasing underlying memory.
    /// Call at the start of each frame to reuse the buffer.
    void reset() noexcept { m_buffer.clear(); }

    void reserve(std::size_t bytes) { m_buffer.reserve(bytes); }

    [[nodiscard]] std::size_t    used()     const noexcept { return m_buffer.size(); }
    [[nodiscard]] std::size_t    capacity() const noexcept { return m_buffer.capacity(); }
    [[nodiscard]] bool           empty()    const noexcept { return m_buffer.empty(); }
    [[nodiscard]] const uint8_t* data()     const noexcept { return m_buffer.data(); }
    [[nodiscard]]       uint8_t* data()           noexcept { return m_buffer.data(); }

private:
    std::vector<uint8_t> m_buffer;
};

} // namespace spt3d
