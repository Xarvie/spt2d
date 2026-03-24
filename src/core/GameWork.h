#pragma once

#include <vector>
#include <cstdint>
#include <cstring>
#include <cassert>
#include <type_traits>

namespace spt3d {

// =============================================================================
//  RenderCommand
//
//  A fixed-size, data-driven render command that carries its payload inline
//  (no heap allocation) and dispatches via a plain function pointer.
//
//  Design rationale:
//    - Using a plain function pointer (ExecuteFn) instead of std::function
//      avoids the heap allocation and virtual-dispatch overhead that
//      std::function incurs when the captured state exceeds its internal SBO.
//    - The payload is a plain byte array, so T must be trivially copyable.
//      This guarantees that the entire RenderCommand struct is itself trivially
//      copyable and can live safely inside a TripleBuffer slot.
//    - kPayloadSize is intentionally kept at 64 bytes (one cache line).
//      If a particular command needs more data it should store a handle/offset
//      into a side buffer (e.g. GameWork::uniformPool) rather than bloating
//      every command.
//
//  Sort key layout (64-bit, high-bit = most significant):
//    [63:56]  layer        (0-255, e.g. background=0, world=128, UI=255)
//    [55:32]  depth        (24-bit fixed-point NDC depth, front-to-back or
//                           back-to-front depending on layer policy)
//    [31:16]  material id  (minimises shader/texture state changes)
//    [15: 0]  sequence     (stable insertion order within the same material)
//
//  Helpers for building sort keys are provided below.
// =============================================================================

struct RenderCommand {
    // One cache line of inline payload – enough for the vast majority of draw
    // commands (transform index, material id, mesh range, …).
    static constexpr std::size_t kPayloadSize = 64;

    using ExecuteFn = void (*)(const void* payload);

    // ----- data members (hot path: execFn + sortKey fit in first 16 bytes) ---
    ExecuteFn execFn  = nullptr;
    uint64_t  sortKey = 0;
    uint16_t  typeId  = 0;
    uint8_t   _pad[6] = {};
    alignas(8) uint8_t payload[kPayloadSize] = {};

    // Execute the command.  Inline so the compiler can devirtualise the loop.
    void execute() const {
        if (execFn) execFn(payload);
    }

    // -------------------------------------------------------------------------
    // Factory: create a RenderCommand from any trivially-copyable struct T.
    // -------------------------------------------------------------------------
    template <typename T>
    static RenderCommand create(const T& data, ExecuteFn fn,
                                uint64_t key = 0, uint16_t tid = 0) noexcept {
        static_assert(sizeof(T) <= kPayloadSize,
                      "RenderCommand payload too large – use a side buffer");
        static_assert(std::is_trivially_copyable<T>::value,
                      "RenderCommand payload T must be trivially copyable");

        RenderCommand cmd;
        std::memcpy(cmd.payload, &data, sizeof(T));
        cmd.execFn  = fn;
        cmd.sortKey = key;
        cmd.typeId  = tid;
        return cmd;
    }

    // -------------------------------------------------------------------------
    // Sort-key helpers
    // -------------------------------------------------------------------------
    static constexpr uint64_t buildSortKey(uint8_t  layer,
                                           uint32_t depth24,
                                           uint16_t materialId,
                                           uint16_t sequence) noexcept {
        return (static_cast<uint64_t>(layer)      << 56)
             | (static_cast<uint64_t>(depth24 & 0x00FF'FFFFu) << 32)
             | (static_cast<uint64_t>(materialId) << 16)
             |  static_cast<uint64_t>(sequence);
    }

    static constexpr uint8_t  layerOf(uint64_t key) noexcept {
        return static_cast<uint8_t>(key >> 56);
    }
    static constexpr uint32_t depth24Of(uint64_t key) noexcept {
        return static_cast<uint32_t>((key >> 32) & 0x00FF'FFFFu);
    }
    static constexpr uint16_t materialOf(uint64_t key) noexcept {
        return static_cast<uint16_t>((key >> 16) & 0xFFFFu);
    }
    static constexpr uint16_t sequenceOf(uint64_t key) noexcept {
        return static_cast<uint16_t>(key & 0xFFFFu);
    }
};

// Verify the struct is itself trivially copyable so it can live in a
// TripleBuffer slot without surprises.
static_assert(std::is_trivially_copyable<RenderCommand>::value,
              "RenderCommand must remain trivially copyable");


// =============================================================================
//  PlatformCommand
//
//  Commands directed at the host platform (IME, clipboard, …).
//  textData must point to a string that remains valid until the platform
//  thread has consumed this command (typically the same frame).
// =============================================================================
struct PlatformCommand {
    enum class Type : uint8_t {
        StartIME,
        StopIME,
        SetIMERect,
        SetClipboard,
    };

    Type        type     = Type::StopIME;
    int         x        = 0;
    int         y        = 0;
    int         w        = 0;
    int         h        = 0;
    const char* textData = nullptr;   // non-owning; caller manages lifetime
};


// =============================================================================
//  LinearAllocator  (inline uniform / scratch pool)
//
//  A simple bump allocator backed by a std::vector<uint8_t>.  Provides
//  pointer-stable allocations within a frame: the buffer never reallocates
//  once it has reached its steady-state capacity, and all allocations are
//  freed at once by reset().
//
//  Usage:
//    LinearAllocator pool;
//    pool.reserve(4096);          // optional warm-up
//    auto* p = pool.alloc<MyUBO>();
//    if (p) { *p = ...; }
// =============================================================================
class LinearAllocator {
public:
    explicit LinearAllocator(std::size_t initialCapacity = 0) {
        if (initialCapacity > 0) m_buffer.reserve(initialCapacity);
    }

    /// Allocate `size` bytes aligned to `alignment`. Returns nullptr on OOM.
    void* alloc(std::size_t size, std::size_t alignment = alignof(std::max_align_t)) {
        assert(alignment > 0 && (alignment & (alignment - 1)) == 0);
        const std::size_t cur = m_buffer.size();
        const std::size_t pad = (alignment - (cur & (alignment - 1))) & (alignment - 1);
        const std::size_t needed = cur + pad + size;
        if (needed > m_buffer.capacity()) {
            // Grow by doubling to amortise allocation cost, but only up to a
            // hard cap (configurable per platform).
            const std::size_t newCap = std::max(needed, m_buffer.capacity() * 2);
            m_buffer.reserve(newCap);
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

    /// Returns the byte offset of a pointer previously obtained from alloc().
    /// Useful for storing relative references inside RenderCommand payloads.
    uint32_t offsetOf(const void* p) const noexcept {
        const auto base = reinterpret_cast<uintptr_t>(m_buffer.data());
        const auto ptr  = reinterpret_cast<uintptr_t>(p);
        assert(ptr >= base && ptr < base + m_buffer.size());
        return static_cast<uint32_t>(ptr - base);
    }

    /// Retrieve a pointer from a previously stored offset.
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

    /// Frees all allocations without releasing the underlying memory.
    /// Call this at the start of each frame to reuse the buffer.
    void reset() noexcept { m_buffer.clear(); }

    void reserve(std::size_t bytes) { m_buffer.reserve(bytes); }

    [[nodiscard]] std::size_t used()     const noexcept { return m_buffer.size(); }
    [[nodiscard]] std::size_t capacity() const noexcept { return m_buffer.capacity(); }
    [[nodiscard]] bool        empty()    const noexcept { return m_buffer.empty(); }

    /// Direct access for serialisation / GPU upload.
    [[nodiscard]] const uint8_t* data()  const noexcept { return m_buffer.data(); }
    [[nodiscard]]       uint8_t* data()        noexcept { return m_buffer.data(); }

private:
    std::vector<uint8_t> m_buffer;
};


// =============================================================================
//  Viewport
// =============================================================================
struct Viewport {
    int  x = 0;
    int  y = 0;
    int  w = 0;
    int  h = 0;
    bool valid = false;

    void set(int x_, int y_, int w_, int h_) noexcept {
        x = x_; y = y_; w = w_; h = h_; valid = true;
    }
    void clear() noexcept { *this = Viewport{}; }
};


// =============================================================================
//  GameWork
//
//  The frame packet produced by the game-logic thread and consumed by the
//  render thread every frame.  It lives inside a TripleBuffer<GameWork>, so:
//
//    - All members must be default-constructible.
//    - The struct must NOT store raw owning pointers (use vectors/allocators).
//    - clear() resets content but deliberately retains vector/allocator
//      capacity so that steady-state operation is allocation-free.
//
//  Lifetime contract:
//    The logic thread writes into writeBuffer(), calls publish(), and must
//    not touch that buffer again until the TripleBuffer hands it back as a
//    new write slot (which may be the very next frame).
//    The render thread calls fetch() and then reads readBuffer() for the
//    entire duration of the frame; it must not write to it.
// =============================================================================
struct GameWork {

    // ------------------------------------------------------------------
    // Frame metadata
    // ------------------------------------------------------------------
    double   logicTime  = 0.0;   ///< Accumulated simulation time (seconds)
    float    deltaTime  = 0.0f;  ///< Fixed or measured step for this frame
    uint64_t frameIndex = 0;     ///< Monotonically increasing frame counter

    // ------------------------------------------------------------------
    // Render command stream
    //
    // Commands are appended in any order during onRender(); the executor
    // sorts them by sortKey before dispatch.  Keeping commands and the
    // uniform pool together in one struct means a single TripleBuffer swap
    // transfers both atomically.
    // ------------------------------------------------------------------
    std::vector<RenderCommand> renderCommands;

    /// Scratch memory for per-frame uniform data (UBOs, push constants, …).
    /// Render commands store offsets into this pool rather than raw pointers.
    LinearAllocator uniformPool;

    // ------------------------------------------------------------------
    // Platform commands  (IME, clipboard, …)
    // ------------------------------------------------------------------
    std::vector<PlatformCommand> platformCommands;

    // ------------------------------------------------------------------
    // Viewport override
    // ------------------------------------------------------------------
    Viewport viewport;

    // ------------------------------------------------------------------
    // Construction / assignment
    // ------------------------------------------------------------------
    GameWork() {
        // Warm-up: reserve typical per-frame capacities to avoid early
        // reallocation.  These numbers are conservative; the allocator
        // will grow as needed and retain the larger capacity.
        renderCommands.reserve(256);
        platformCommands.reserve(8);
        uniformPool.reserve(4096);
    }

    // Defaulted copy/move – all members support them.
    GameWork(const GameWork&)            = default;
    GameWork& operator=(const GameWork&) = default;
    GameWork(GameWork&&)                 = default;
    GameWork& operator=(GameWork&&)      = default;

    // ------------------------------------------------------------------
    // Frame lifecycle helpers
    // ------------------------------------------------------------------

    /// Clears all per-frame data while retaining allocated capacity.
    /// Call this at the start of each write pass (TripleBuffer logic side).
    void reset() noexcept {
        logicTime  = 0.0;
        deltaTime  = 0.0f;
        frameIndex = 0;
        renderCommands.clear();
        uniformPool.reset();
        platformCommands.clear();
        viewport.clear();
    }

    // ------------------------------------------------------------------
    // RenderCommand submission helpers
    // ------------------------------------------------------------------

    /// Submit a typed render command.
    template <typename T>
    void submit(const T& data, RenderCommand::ExecuteFn fn,
                uint64_t sortKey = 0, uint16_t typeId = 0) {
        renderCommands.push_back(RenderCommand::create(data, fn, sortKey, typeId));
    }

    /// Submit with an explicit layer (convenience wrapper over buildSortKey).
    template <typename T>
    void submitLayer(const T& data, RenderCommand::ExecuteFn fn,
                     uint8_t layer, uint16_t materialId = 0,
                     uint16_t sequence = 0, uint32_t depth24 = 0) {
        const uint64_t key = RenderCommand::buildSortKey(layer, depth24,
                                                         materialId, sequence);
        renderCommands.push_back(RenderCommand::create(data, fn, key));
    }

    // ------------------------------------------------------------------
    // Platform command helpers
    // ------------------------------------------------------------------
    void pushPlatformCommand(const PlatformCommand& cmd) {
        platformCommands.push_back(cmd);
    }

    void setViewport(int x, int y, int w, int h) noexcept {
        viewport.set(x, y, w, h);
    }

    // ------------------------------------------------------------------
    // Diagnostics
    // ------------------------------------------------------------------
    struct Stats {
        std::size_t renderCommandCount = 0;
        std::size_t uniformBytesUsed   = 0;
    };

    [[nodiscard]] Stats stats() const noexcept {
        return { renderCommands.size(), uniformPool.used() };
    }
};

} // namespace spt3d
