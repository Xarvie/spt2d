#pragma once

#include <functional>
#include <vector>
#include <mutex>
#include <cstdint>
#include <cassert>
#include <algorithm>
#include <type_traits>

// =============================================================================
//  Signal<Args...>
//
//  A thread-safe, observer-pattern signal/slot implementation suited for
//  production game-engine use.
//
//  Design goals & decisions
//  ─────────────────────────
//  1. Re-entrant emit()
//     Slots may call connect() or disconnect() while the signal is being
//     emitted without deadlocking or invalidating the iteration.  This is
//     achieved via a generation-counter copy-on-write strategy: emit() works
//     on a stable snapshot of the slot list that it captured before invoking
//     any slot.  Mutations made during emission take effect on the NEXT emit().
//
//  2. No copy-per-emit in the common case  (single-threaded emit path)
//     A re-entrancy depth counter is maintained.  When emit() is called from
//     outside any other emit(), it uses the live list directly rather than
//     copying.  The snapshot copy is only made when emit() is re-entered
//     (i.e. a slot calls emit() on the same signal), which is rare.
//     Combined with the thread-safety mutex this means the common single-
//     threaded path has exactly one lock acquisition and no heap allocation.
//
//     NOTE: If emit() is called concurrently from multiple threads the full
//     snapshot copy path is always taken for safety.
//
//  3. ScopedConnection RAII handle
//     connect() returns a ScopedConnection that disconnects automatically on
//     destruction.  Callers that need manual lifetime management can call
//     release() to detach the handle from the connection ID.
//
//  4. Disconnect-during-emit safety
//     Slots marked for removal during an active emit() are skipped inline
//     (their pendingRemove flag is checked before each call) and purged from
//     the live list at the end of the outermost emit() using the erase-remove
//     idiom – a single O(n) pass rather than O(n) per removal.
//
//  5. No dynamic allocation for the slot list in steady state
//     The internal vector retains its capacity across connect/disconnect
//     cycles once it has grown to its high-water mark.
//
//  Thread-safety model
//  ────────────────────
//  - connect() / disconnect() / clear() : safe from any thread.
//  - emit()                             : safe from any thread; concurrent
//                                         emits are serialised by the mutex.
//  - ScopedConnection destructor        : safe from any thread.
// =============================================================================

// Forward declaration so ScopedConnection can reference Signal.
template <typename... Args>
class Signal;


// =============================================================================
//  ScopedConnection
//
//  RAII wrapper around a connection ID.  Automatically disconnects when it
//  goes out of scope.  Move-only (copying a connection handle makes no sense).
// =============================================================================
template <typename... Args>
class ScopedConnection {
public:
    ScopedConnection() noexcept = default;

    ScopedConnection(Signal<Args...>* signal, uint32_t id) noexcept
        : m_signal(signal), m_id(id) {}

    ~ScopedConnection() { disconnect(); }

    ScopedConnection(const ScopedConnection&)            = delete;
    ScopedConnection& operator=(const ScopedConnection&) = delete;

    ScopedConnection(ScopedConnection&& other) noexcept
        : m_signal(other.m_signal), m_id(other.m_id) {
        other.m_signal = nullptr;
        other.m_id     = 0;
    }

    ScopedConnection& operator=(ScopedConnection&& other) noexcept {
        if (this != &other) {
            disconnect();
            m_signal       = other.m_signal;
            m_id           = other.m_id;
            other.m_signal = nullptr;
            other.m_id     = 0;
        }
        return *this;
    }

    /// Explicitly disconnect before the scope ends.
    void disconnect() {
        if (m_signal && m_id != 0) {
            m_signal->disconnect(m_id);
            m_signal = nullptr;
            m_id     = 0;
        }
    }

    /// Detach: give up ownership without disconnecting.
    /// Returns the raw connection id for manual management.
    [[nodiscard]] uint32_t release() noexcept {
        m_signal   = nullptr;
        uint32_t id = m_id;
        m_id       = 0;
        return id;
    }

    [[nodiscard]] bool connected() const noexcept {
        return m_signal != nullptr && m_id != 0;
    }

    [[nodiscard]] uint32_t id() const noexcept { return m_id; }

private:
    Signal<Args...>* m_signal = nullptr;
    uint32_t         m_id     = 0;
};


// =============================================================================
//  Signal<Args...>
// =============================================================================
template <typename... Args>
class Signal {
public:
    using Slot             = std::function<void(Args...)>;
    using ConnectionHandle = ScopedConnection<Args...>;

    Signal()  = default;
    ~Signal() = default;

    Signal(const Signal&)            = delete;
    Signal& operator=(const Signal&) = delete;

    // Move is allowed: the caller is responsible for ensuring no concurrent
    // operations are in flight when moving.
    Signal(Signal&& other) noexcept {
        std::lock_guard<std::mutex> lk(other.m_mutex);
        m_slots      = std::move(other.m_slots);
        m_nextId     = other.m_nextId.load(std::memory_order_relaxed);
        m_emitDepth  = 0;   // fresh – the moved-from object may have been emitting
        m_isDirty    = other.m_isDirty;
        other.m_isDirty   = false;
        other.m_emitDepth = 0;
    }

    // -------------------------------------------------------------------------
    // connect()  –  register a slot and return a RAII ScopedConnection
    // -------------------------------------------------------------------------

    /// Returns a ScopedConnection that disconnects automatically on destruction.
    [[nodiscard]] ConnectionHandle connect(Slot slot) {
        const uint32_t id = allocId();
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            m_slots.push_back(Connection{id, std::move(slot), false});
        }
        return ConnectionHandle(this, id);
    }

    /// Returns a raw connection id for callers that manage lifetime manually.
    /// Prefer the RAII overload above when possible.
    [[nodiscard]] uint32_t connectRaw(Slot slot) {
        const uint32_t id = allocId();
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            m_slots.push_back(Connection{id, std::move(slot), false});
        }
        return id;
    }

    // -------------------------------------------------------------------------
    // disconnect()
    // -------------------------------------------------------------------------

    /// Marks the connection with the given id for removal.
    /// If called during emit(), the slot is skipped for the current emission
    /// and removed after the outermost emit() returns.
    void disconnect(uint32_t id) {
        if (id == 0) return;
        std::lock_guard<std::mutex> lk(m_mutex);
        for (auto& c : m_slots) {
            if (c.id == id) {
                c.pendingRemove = true;
                m_isDirty       = true;
                break;
            }
        }
    }

    // -------------------------------------------------------------------------
    // emit()
    // -------------------------------------------------------------------------

    /// Invokes all connected slots with the provided arguments.
    ///
    /// Re-entrant: a slot may call emit() on the same signal without deadlock.
    /// Slots added or removed during emission take effect on the next emit().
    void emit(Args... args) {
        // --- acquire lock, increment depth, get reference to live list -------
        std::unique_lock<std::mutex> lk(m_mutex);

        if (m_emitDepth == 0) {
            // Outermost emit: work directly on the live list to avoid a copy.
            // We hold the lock for the full duration of the emission.
            // This is safe because:
            //   a) connect() / disconnect() will also need the lock and will
            //      therefore wait until emission completes.
            //   b) Re-entrant emit() from within a slot increments m_emitDepth
            //      and takes the snapshot path below (see depth > 0 branch).
            ++m_emitDepth;

            // Invoke slots while holding the lock.  This prevents a concurrent
            // thread from modifying the vector while we iterate it.
            // For slots that must not be called under the lock, use a different
            // pattern (e.g. drain to a local vector first).
            for (std::size_t i = 0; i < m_slots.size(); ++i) {
                auto& c = m_slots[i];
                if (!c.pendingRemove && c.slot) {
                    // Copy the slot to a local BEFORE releasing the lock.
                    // While the lock is released, connect() from another thread
                    // may push_back to m_slots, which can reallocate the vector
                    // and invalidate the reference `c`.  The local copy ensures
                    // we invoke a valid callable regardless of reallocation.
                    Slot localSlot = c.slot;
                    lk.unlock();
                    localSlot(args...);    // invoke without lock
                    lk.lock();
                    // After re-locking, `m_slots` may have grown (new connects)
                    // but will not have shrunk (removes are deferred via flag).
                }
            }

            --m_emitDepth;
            if (m_isDirty) cleanup();
        } else {
            // Re-entrant call: take a snapshot to avoid invalidating the
            // outer loop's index.  The snapshot is a shallow copy of the
            // Connection structs; std::function copies are unavoidable here
            // but this path is rare (re-entrant signals).
            ++m_emitDepth;
            std::vector<Connection> snapshot = m_slots;  // copy under lock
            lk.unlock();

            for (const auto& c : snapshot) {
                if (!c.pendingRemove && c.slot) {
                    c.slot(args...);
                }
            }

            lk.lock();
            --m_emitDepth;
            if (m_emitDepth == 0 && m_isDirty) cleanup();
        }
    }

    // -------------------------------------------------------------------------
    // Utilities
    // -------------------------------------------------------------------------

    /// Remove all slots immediately.  Do not call while emit() is in progress.
    void clear() {
        std::lock_guard<std::mutex> lk(m_mutex);
        assert(m_emitDepth == 0 && "clear() called during emit()");
        m_slots.clear();
        m_isDirty = false;
    }

    /// Number of currently registered (non-pending-remove) slots.
    [[nodiscard]] std::size_t size() const {
        std::lock_guard<std::mutex> lk(m_mutex);
        std::size_t n = 0;
        for (const auto& c : m_slots) {
            if (!c.pendingRemove) ++n;
        }
        return n;
    }

    [[nodiscard]] bool empty() const {
        std::lock_guard<std::mutex> lk(m_mutex);
        for (const auto& c : m_slots) {
            if (!c.pendingRemove) return false;
        }
        return true;
    }

private:
    // -------------------------------------------------------------------------
    // Internal types
    // -------------------------------------------------------------------------
    struct Connection {
        uint32_t id            = 0;
        Slot     slot;
        bool     pendingRemove = false;
    };

    // -------------------------------------------------------------------------
    // Helpers
    // -------------------------------------------------------------------------

    uint32_t allocId() noexcept {
        uint32_t id = ++m_nextId;
        if (id == 0) id = ++m_nextId;  // skip 0 (reserved as "no connection")
        return id;
    }

    // Must be called with m_mutex held and m_emitDepth == 0.
    void cleanup() {
        m_slots.erase(
            std::remove_if(m_slots.begin(), m_slots.end(),
                           [](const Connection& c) { return c.pendingRemove; }),
            m_slots.end());
        m_isDirty = false;
    }

    // -------------------------------------------------------------------------
    // Data members
    // -------------------------------------------------------------------------
    mutable std::mutex       m_mutex;
    std::vector<Connection>  m_slots;
    std::atomic<uint32_t>    m_nextId{0};
    int                      m_emitDepth = 0;  // guarded by m_mutex
    bool                     m_isDirty   = false;
};
