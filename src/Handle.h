// spt3d/Handle.h — GPU resource handles + thread-safe handle allocator
// [THREAD: any] — Handles are plain uint32_t wrappers; safe from any thread.
//
// Design:
//   Bits [15:0]  = index into GPUDevice's slot map (max 65535 resources per type)
//   Bits [23:16] = generation counter (detects use-after-free, wraps at 255)
//   Bits [31:24] = tag (user-defined grouping for batch operations)
//
// Handle allocation flow (multi-threaded):
//   1. Logic thread calls HandleAllocator::allocate() → gets a valid handle immediately.
//   2. Logic thread submits a ResourceTask carrying {handle, CPU data} into ResourceQueue.
//   3. Logic thread can use the handle in DrawItem / MaterialSnapshot right away.
//   4. Render thread drains ResourceQueue, creates GL object, maps handle → GL ID.
//
// Handle allocation flow (single-threaded / pre-registration):
//   Same as above, but both steps happen on the main thread sequentially.
#pragma once

#include <cstdint>
#include <vector>
#include <atomic>

namespace spt3d {

namespace detail {

// Generic handle with type safety via tag template parameter.
// All handles have identical layout; the template just prevents mixing types.
template <typename Tag>
struct HandleT {
    uint32_t value = 0;

    uint16_t Index()      const { return static_cast<uint16_t>(value & 0xFFFF); }
    uint8_t  Generation() const { return static_cast<uint8_t>((value >> 16) & 0xFF); }
    uint8_t  UserTag()    const { return static_cast<uint8_t>((value >> 24) & 0xFF); }
    bool     Valid()      const { return value != 0; }

    explicit operator bool() const { return value != 0; }

    bool operator==(HandleT o) const { return value == o.value; }
    bool operator!=(HandleT o) const { return value != o.value; }

    static HandleT Make(uint16_t idx, uint8_t gen, uint8_t tag = 0) {
        return HandleT{
            (static_cast<uint32_t>(tag) << 24) |
            (static_cast<uint32_t>(gen) << 16) |
            static_cast<uint32_t>(idx)
        };
    }
};

// Tag types for compile-time handle safety
struct MeshTag {};
struct TexTag {};
struct ShaderTag {};
struct RTTag {};

} // namespace detail

// =========================================================================
//  Public handle types
// =========================================================================

using MeshHandle   = detail::HandleT<detail::MeshTag>;
using TexHandle    = detail::HandleT<detail::TexTag>;
using ShaderHandle = detail::HandleT<detail::ShaderTag>;
using RTHandle     = detail::HandleT<detail::RTTag>;

// Invalid sentinel (all types, value == 0)
inline constexpr MeshHandle   kNullMesh   {};
inline constexpr TexHandle    kNullTex    {};
inline constexpr ShaderHandle kNullShader {};
inline constexpr RTHandle     kNullRT     {};

// =========================================================================
//  HandleAllocator — thread-safe handle ID generator
//  [THREAD: any] — lock-free, safe to call from logic or main thread.
//
//  The logic thread calls allocate() to mint a handle BEFORE submitting
//  a resource creation task. The render thread later maps the handle to
//  a GL object. The handle is usable immediately (e.g. in DrawItem).
//
//  Index 0 is reserved for null. Generation is always 1 for newly
//  allocated handles. On destruction+recreation at the same index,
//  GPUDevice bumps the generation in its internal SlotMap.
// =========================================================================

template <typename Tag>
class HandleAllocator {
public:
    /// Allocate a new unique handle. Thread-safe, lock-free.
    detail::HandleT<Tag> allocate(uint8_t userTag = 0) noexcept {
        uint16_t idx = m_nextIndex.fetch_add(1, std::memory_order_relaxed);
        return detail::HandleT<Tag>::Make(idx, 1, userTag);
    }

    /// Current high-water mark (for diagnostics).
    uint16_t allocated() const noexcept {
        return m_nextIndex.load(std::memory_order_relaxed) - 1;
    }

    /// Reset to initial state. NOT thread-safe — call only during init/shutdown.
    void reset() noexcept { m_nextIndex.store(1, std::memory_order_relaxed); }

private:
    std::atomic<uint16_t> m_nextIndex{1};   // 0 = null handle
};

// Convenience aliases
using MeshHandleAlloc   = HandleAllocator<detail::MeshTag>;
using TexHandleAlloc    = HandleAllocator<detail::TexTag>;
using ShaderHandleAlloc = HandleAllocator<detail::ShaderTag>;
using RTHandleAlloc     = HandleAllocator<detail::RTTag>;

// =========================================================================
//  SlotMap — generic handle→object mapping (used by GPUDevice internally)
//
//  Not exposed in the public API; defined here so both GPUDevice.h and
//  unit tests can include it without circular dependencies.
//
//  Supports two modes:
//    insert(obj)          — self-allocates index (pre-registration on render thread)
//    insertAt(handle,obj) — places obj at a pre-allocated index (async creation)
// =========================================================================

template <typename T>
class SlotMap {
public:
    struct Slot {
        T       obj{};
        uint8_t generation = 1;   // starts at 1 so handle gen=0 is always stale
        bool    occupied   = false;
    };

    /// Insert an object; returns a raw handle value (index | gen<<16).
    uint32_t insert(T obj) {
        uint16_t idx;
        if (!m_freeList.empty()) {
            idx = m_freeList.back();
            m_freeList.pop_back();
        } else {
            idx = static_cast<uint16_t>(m_slots.size());
            m_slots.push_back(Slot{});
        }
        auto& slot      = m_slots[idx];
        slot.obj        = std::move(obj);
        slot.occupied   = true;
        return static_cast<uint32_t>(idx) |
               (static_cast<uint32_t>(slot.generation) << 16);
    }

    /// Insert at a pre-allocated handle index (from HandleAllocator).
    /// Grows the slot array if needed. Returns true on success.
    bool insertAt(uint32_t handle, T obj) {
        const uint16_t idx = static_cast<uint16_t>(handle & 0xFFFF);
        const uint8_t  gen = static_cast<uint8_t>((handle >> 16) & 0xFF);

        // Grow if needed
        while (idx >= m_slots.size()) {
            m_slots.push_back(Slot{});
        }

        auto& slot = m_slots[idx];
        if (slot.occupied) return false;   // already occupied — stale or duplicate

        slot.obj        = std::move(obj);
        slot.generation = gen;
        slot.occupied   = true;
        return true;
    }

    /// Retrieve by handle value. Returns nullptr if generation mismatch or out-of-range.
    T* get(uint32_t handle) {
        const uint16_t idx = static_cast<uint16_t>(handle & 0xFFFF);
        const uint8_t  gen = static_cast<uint8_t>((handle >> 16) & 0xFF);
        if (idx >= m_slots.size()) return nullptr;
        auto& slot = m_slots[idx];
        if (!slot.occupied || slot.generation != gen) return nullptr;
        return &slot.obj;
    }

    const T* get(uint32_t handle) const {
        return const_cast<SlotMap*>(this)->get(handle);
    }

    /// Remove by handle value. Increments generation so stale handles are rejected.
    void remove(uint32_t handle) {
        const uint16_t idx = static_cast<uint16_t>(handle & 0xFFFF);
        const uint8_t  gen = static_cast<uint8_t>((handle >> 16) & 0xFF);
        if (idx >= m_slots.size()) return;
        auto& slot = m_slots[idx];
        if (!slot.occupied || slot.generation != gen) return;
        slot.obj      = T{};
        slot.occupied  = false;
        slot.generation++;       // wraps at 255→0, which is fine
        m_freeList.push_back(idx);
    }

    bool isValid(uint32_t handle) const { return get(handle) != nullptr; }

    std::size_t size()     const { return m_slots.size() - m_freeList.size(); }
    std::size_t capacity() const { return m_slots.size(); }

    /// Iterate all occupied slots (for bulk cleanup on shutdown).
    template <typename Fn>
    void forEach(Fn&& fn) {
        for (auto& slot : m_slots) {
            if (slot.occupied) fn(slot.obj);
        }
    }

private:
    std::vector<Slot>     m_slots;
    std::vector<uint16_t> m_freeList;
};

} // namespace spt3d
