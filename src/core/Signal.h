// spt3d/core/Signal.h — Thread-safe signal/slot
// [THREAD: any] — All methods are safe from any thread.
//
// Fixed vs original:
//   - emit() snapshots slot count before iteration so newly-connected
//     slots during emission are deferred to the NEXT emit (matches docs).
//   - Re-entrant emit uses snapshot copy (rare path).
//   - ScopedConnection for RAII disconnect.
#pragma once

#include <functional>
#include <vector>
#include <mutex>
#include <cstdint>
#include <algorithm>
#include <atomic>

namespace spt3d {

template <typename... Args> class Signal;

// =========================================================================
//  ScopedConnection — RAII disconnect handle (move-only)
// =========================================================================
template <typename... Args>
class ScopedConnection {
public:
    ScopedConnection() noexcept = default;
    ScopedConnection(Signal<Args...>* sig, uint32_t id) noexcept : m_sig(sig), m_id(id) {}
    ~ScopedConnection() { disconnect(); }

    ScopedConnection(const ScopedConnection&)            = delete;
    ScopedConnection& operator=(const ScopedConnection&) = delete;

    ScopedConnection(ScopedConnection&& o) noexcept : m_sig(o.m_sig), m_id(o.m_id) {
        o.m_sig = nullptr; o.m_id = 0;
    }
    ScopedConnection& operator=(ScopedConnection&& o) noexcept {
        if (this != &o) { disconnect(); m_sig = o.m_sig; m_id = o.m_id; o.m_sig = nullptr; o.m_id = 0; }
        return *this;
    }

    void disconnect() {
        if (m_sig && m_id) { m_sig->disconnect(m_id); m_sig = nullptr; m_id = 0; }
    }

    [[nodiscard]] uint32_t release() noexcept {
        auto id = m_id; m_sig = nullptr; m_id = 0; return id;
    }

    [[nodiscard]] bool     connected() const noexcept { return m_sig && m_id; }
    [[nodiscard]] uint32_t id()        const noexcept { return m_id; }

private:
    Signal<Args...>* m_sig = nullptr;
    uint32_t         m_id  = 0;
};

// =========================================================================
//  Signal<Args...>
// =========================================================================
template <typename... Args>
class Signal {
public:
    using Slot   = std::function<void(Args...)>;
    using Handle = ScopedConnection<Args...>;

    Signal()  = default;
    ~Signal() = default;
    Signal(const Signal&)            = delete;
    Signal& operator=(const Signal&) = delete;

    // ── connect ─────────────────────────────────────────────────────────

    [[nodiscard]] Handle connect(Slot slot) {
        const uint32_t id = allocId();
        { std::lock_guard<std::mutex> lk(m_mutex);
          m_slots.push_back({id, std::move(slot), false}); }
        return Handle(this, id);
    }

    [[nodiscard]] uint32_t connectRaw(Slot slot) {
        const uint32_t id = allocId();
        { std::lock_guard<std::mutex> lk(m_mutex);
          m_slots.push_back({id, std::move(slot), false}); }
        return id;
    }

    // ── disconnect ──────────────────────────────────────────────────────

    void disconnect(uint32_t id) {
        if (!id) return;
        std::lock_guard<std::mutex> lk(m_mutex);
        for (auto& c : m_slots)
            if (c.id == id) { c.pendingRemove = true; m_isDirty = true; break; }
    }

    // ── emit ────────────────────────────────────────────────────────────

    void emit(Args... args) {
        std::unique_lock<std::mutex> lk(m_mutex);

        if (m_emitDepth == 0) {
            // Outermost emit: iterate live list by index.
            // Snapshot the count so slots added during this emit are NOT visited.
            ++m_emitDepth;
            const std::size_t count = m_slots.size();

            for (std::size_t i = 0; i < count; ++i) {
                if (m_slots[i].pendingRemove || !m_slots[i].slot) continue;
                // Copy the callable before releasing the lock. After unlock,
                // the vector may reallocate (via connect on another thread),
                // but we hold a local copy and never touch m_slots[i] again
                // until we re-acquire the lock and advance to the next index.
                Slot local = m_slots[i].slot;
                lk.unlock();
                local(args...);
                lk.lock();
            }

            --m_emitDepth;
            if (m_isDirty) cleanup();
        } else {
            // Re-entrant: work on a snapshot to avoid invalidating outer loop.
            ++m_emitDepth;
            std::vector<Connection> snapshot = m_slots;
            lk.unlock();
            for (const auto& c : snapshot)
                if (!c.pendingRemove && c.slot) c.slot(args...);
            lk.lock();
            --m_emitDepth;
            if (m_emitDepth == 0 && m_isDirty) cleanup();
        }
    }

    // ── utilities ───────────────────────────────────────────────────────

    void clear() {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_slots.clear(); m_isDirty = false;
    }

    [[nodiscard]] std::size_t size() const {
        std::lock_guard<std::mutex> lk(m_mutex);
        std::size_t n = 0;
        for (const auto& c : m_slots) if (!c.pendingRemove) ++n;
        return n;
    }

    [[nodiscard]] bool empty() const {
        std::lock_guard<std::mutex> lk(m_mutex);
        for (const auto& c : m_slots) if (!c.pendingRemove) return false;
        return true;
    }

private:
    struct Connection { uint32_t id = 0; Slot slot; bool pendingRemove = false; };

    uint32_t allocId() noexcept {
        uint32_t id = ++m_nextId;
        if (id == 0) id = ++m_nextId;
        return id;
    }

    void cleanup() {
        m_slots.erase(
            std::remove_if(m_slots.begin(), m_slots.end(),
                           [](const Connection& c){ return c.pendingRemove; }),
            m_slots.end());
        m_isDirty = false;
    }

    mutable std::mutex      m_mutex;
    std::vector<Connection> m_slots;
    std::atomic<uint32_t>   m_nextId{0};
    int                     m_emitDepth = 0;
    bool                    m_isDirty   = false;
};

} // namespace spt3d
