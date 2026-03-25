#pragma once

#include <functional>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <optional>
#include <cstdint>
#include <cassert>
#include <limits>

// =============================================================================
//  CallbackManager<ResultType>
//
//  Manages a set of one-shot callbacks that are each associated with a unique
//  token (handle).  The canonical use case is async request/response flows:
//
//    // Issue a request, remember the token.
//    auto token = callbacks.add([](const Response& r){ ... });
//    network.sendRequest(requestId, token.id());
//
//    // When the response arrives on any thread:
//    callbacks.invoke(token.id(), response);
//
//    // If the response never arrives, tick() fires the timeout handler.
//    callbacks.tick(dt);
//
//  Design decisions vs. the original implementation
//  ─────────────────────────────────────────────────
//  1.  Per-entry timeout handler
//      The original passed a single optional<ResultType> to tick(), meaning
//      every timed-out callback received the same synthetic result.  Real
//      async systems often need per-request error payloads (e.g. different
//      error codes).  Each Entry now stores its own optional timeout handler
//      (TimeoutFn = std::function<void()>) which is called with no arguments,
//      giving the registrant full control over error handling.
//
//  2.  double accumulator for time
//      The original used float, which loses sub-millisecond precision after
//      ~1 hour of play.  Switched to double; the per-entry expireTime is also
//      double so comparisons are exact.
//
//  3.  cancel()  –  explicit early removal without invoking the callback
//      Useful when the requester is destroyed before the response arrives.
//
//  4.  ScopedCallback  RAII handle
//      add() returns a ScopedCallback that calls cancel() automatically on
//      destruction.  release() hands off ownership for callers that manage
//      lifetime manually.
//
//  5.  isPending()  –  non-destructive query
//
//  6.  tick() returns a TickStats struct with diagnostics.
//
//  7.  Expired callbacks are collected under the lock and invoked outside it,
//      matching the pattern in invoke().  The two parallel vectors have been
//      replaced by a single vector<ExpiredEntry> to keep data together.
//
//  Thread-safety model
//  ────────────────────
//  All public methods are safe to call from any thread.
//  Callbacks and timeout handlers are invoked OUTSIDE the internal mutex so
//  that they may themselves call add(), cancel(), or invoke() without deadlock.
// =============================================================================

template <typename ResultType>
class CallbackManager;   // forward for ScopedCallback

// =============================================================================
//  ScopedCallback  –  RAII ownership of a pending callback registration
// =============================================================================
template <typename ResultType>
class ScopedCallback {
public:
    ScopedCallback() noexcept = default;

    ScopedCallback(CallbackManager<ResultType>* mgr, uint32_t id) noexcept
        : m_mgr(mgr), m_id(id) {}

    ~ScopedCallback() { cancel(); }

    ScopedCallback(const ScopedCallback&)            = delete;
    ScopedCallback& operator=(const ScopedCallback&) = delete;

    ScopedCallback(ScopedCallback&& o) noexcept
        : m_mgr(o.m_mgr), m_id(o.m_id) {
        o.m_mgr = nullptr;
        o.m_id  = 0;
    }

    ScopedCallback& operator=(ScopedCallback&& o) noexcept {
        if (this != &o) {
            cancel();
            m_mgr  = o.m_mgr;
            m_id   = o.m_id;
            o.m_mgr = nullptr;
            o.m_id  = 0;
        }
        return *this;
    }

    /// Cancel the pending callback without invoking it.
    void cancel() {
        if (m_mgr && m_id != 0) {
            m_mgr->cancel(m_id);
            m_mgr = nullptr;
            m_id  = 0;
        }
    }

    /// Give up RAII ownership; the caller is now responsible for invoking or
    /// cancelling this id.  Returns the raw id.
    [[nodiscard]] uint32_t release() noexcept {
        m_mgr      = nullptr;
        uint32_t id = m_id;
        m_id       = 0;
        return id;
    }

    [[nodiscard]] uint32_t id()      const noexcept { return m_id; }
    [[nodiscard]] bool     valid()   const noexcept { return m_id != 0 && m_mgr != nullptr; }

private:
    CallbackManager<ResultType>* m_mgr = nullptr;
    uint32_t                     m_id  = 0;
};


// =============================================================================
//  CallbackManager<ResultType>
// =============================================================================
template <typename ResultType>
class CallbackManager {
public:
    using Callback  = std::function<void(const ResultType&)>;
    using TimeoutFn = std::function<void()>;
    using Handle    = ScopedCallback<ResultType>;

    static constexpr float  kDefaultTimeoutSeconds = 30.0f;
    static constexpr uint32_t kInvalidId           = 0u;

    CallbackManager()  = default;
    ~CallbackManager() = default;

    CallbackManager(const CallbackManager&)            = delete;
    CallbackManager& operator=(const CallbackManager&) = delete;

    // -------------------------------------------------------------------------
    // Registration
    // -------------------------------------------------------------------------

    /// Register a one-shot callback with an optional per-entry timeout handler
    /// and timeout duration.  Returns a RAII ScopedCallback.
    ///
    /// \param onResult   Called with the result when invoke() is called.
    /// \param onTimeout  Called (with no args) when the entry expires during
    ///                   tick().  Pass nullptr / empty function to silently
    ///                   discard timed-out entries.
    /// \param timeoutSec Per-entry timeout in seconds.  Pass a negative value
    ///                   to disable timeout for this entry (it will live until
    ///                   invoke() or cancel() is called explicitly).
    [[nodiscard]] Handle add(Callback  onResult,
                             TimeoutFn onTimeout   = nullptr,
                             float     timeoutSec  = kDefaultTimeoutSeconds) {
        const uint32_t id = allocId();
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            const double expireAt = (timeoutSec >= 0.0f)
                                  ? m_clock + static_cast<double>(timeoutSec)
                                  : std::numeric_limits<double>::infinity();
            m_entries.emplace(id, Entry{std::move(onResult),
                                        std::move(onTimeout),
                                        expireAt});
        }
        return Handle(this, id);
    }

    /// Same as add() but returns the raw id for callers that manage lifetime
    /// manually (e.g. when the id must be sent over a network before the
    /// ScopedCallback is safely storable).
    [[nodiscard]] uint32_t addRaw(Callback  onResult,
                                   TimeoutFn onTimeout  = nullptr,
                                   float     timeoutSec = kDefaultTimeoutSeconds) {
        return add(std::move(onResult), std::move(onTimeout), timeoutSec)
               .release();
    }

    // -------------------------------------------------------------------------
    // Invocation
    // -------------------------------------------------------------------------

    /// Invoke the callback registered under `id` with the given result, then
    /// remove the entry.  If `id` is unknown (already invoked, cancelled, or
    /// never registered) this is a no-op.
    ///
    /// The callback is invoked OUTSIDE the internal lock so it may safely call
    /// add(), cancel(), or invoke() on this manager.
    void invoke(uint32_t id, const ResultType& result) {
        Callback cb = extractCallback(id);
        if (cb) cb(result);
    }

    /// Rvalue overload to avoid an extra copy when the result is temporary.
    void invoke(uint32_t id, ResultType&& result) {
        Callback cb = extractCallback(id);
        if (cb) cb(result);
    }

    // -------------------------------------------------------------------------
    // Cancellation
    // -------------------------------------------------------------------------

    /// Remove the entry without invoking any callback.
    /// Safe to call from any thread; idempotent if the id is unknown.
    void cancel(uint32_t id) {
        if (id == kInvalidId) return;
        std::lock_guard<std::mutex> lk(m_mutex);
        m_entries.erase(id);
    }

    // -------------------------------------------------------------------------
    // Tick  (call once per frame on the owning thread)
    // -------------------------------------------------------------------------

    struct TickStats {
        uint32_t expired = 0;   ///< Number of entries that timed out this tick
        uint32_t pending = 0;   ///< Number of entries still waiting
    };

    /// Advance the internal clock by `dt` seconds and fire timeout handlers
    /// for any entries whose deadline has passed.
    ///
    /// Timeout handlers are invoked OUTSIDE the internal lock.
    TickStats tick(float dt) {
        assert(dt >= 0.0f && "CallbackManager::tick() called with negative dt");

        // --- advance clock and collect expired entries under the lock --------
        std::vector<ExpiredEntry> expired;
        uint32_t pending = 0;
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            m_clock += static_cast<double>(dt);

            for (auto it = m_entries.begin(); it != m_entries.end(); ) {
                if (m_clock >= it->second.expireAt) {
                    expired.push_back({std::move(it->second.onTimeout)});
                    it = m_entries.erase(it);
                } else {
                    ++it;
                    ++pending;
                }
            }
        }

        // --- invoke timeout handlers outside the lock ------------------------
        for (auto& e : expired) {
            if (e.onTimeout) e.onTimeout();
        }

        return TickStats{static_cast<uint32_t>(expired.size()), pending};
    }

    // -------------------------------------------------------------------------
    // Queries
    // -------------------------------------------------------------------------

    [[nodiscard]] bool isPending(uint32_t id) const {
        std::lock_guard<std::mutex> lk(m_mutex);
        return m_entries.count(id) > 0;
    }

    [[nodiscard]] std::size_t size() const {
        std::lock_guard<std::mutex> lk(m_mutex);
        return m_entries.size();
    }

    [[nodiscard]] bool empty() const {
        std::lock_guard<std::mutex> lk(m_mutex);
        return m_entries.empty();
    }

    // -------------------------------------------------------------------------
    // Lifecycle
    // -------------------------------------------------------------------------

    /// Cancel all pending entries without invoking any callbacks.
    void clear() {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_entries.clear();
    }

    /// Reset clock and all state.  Do not call while other threads are using
    /// this manager.
    void reset() {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_entries.clear();
        m_clock  = 0.0;
        m_nextId = 0;
    }

    /// Current value of the internal clock (seconds since construction / last
    /// reset).  Useful for computing custom deadlines.
    [[nodiscard]] double clock() const {
        std::lock_guard<std::mutex> lk(m_mutex);
        return m_clock;
    }

private:
    // -------------------------------------------------------------------------
    // Internal types
    // -------------------------------------------------------------------------

    struct Entry {
        Callback  onResult;
        TimeoutFn onTimeout;
        double    expireAt = std::numeric_limits<double>::infinity();
    };

    struct ExpiredEntry {
        TimeoutFn onTimeout;
    };

    // -------------------------------------------------------------------------
    // Helpers
    // -------------------------------------------------------------------------

    uint32_t allocId() noexcept {
        // On overflow the counter wraps to 0; skip it (reserved as invalid).
        // Also skip any id that is still in the pending map to avoid
        // silently overwriting a live callback after ~4 billion allocations.
        uint32_t id = ++m_nextId;
        if (id == kInvalidId) id = ++m_nextId;   // skip 0

        // Collision check — only relevant after full uint32 wrap-around.
        // In practice this loop almost never executes; if it does it will
        // terminate quickly because the pending map is small.
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            while (m_entries.count(id)) {
                id = ++m_nextId;
                if (id == kInvalidId) id = ++m_nextId;
            }
        }
        return id;
    }

    /// Extract and remove the callback for `id` under the lock; returns it
    /// so the caller can invoke it outside the lock.
    Callback extractCallback(uint32_t id) {
        if (id == kInvalidId) return nullptr;
        Callback cb;
        std::lock_guard<std::mutex> lk(m_mutex);
        auto it = m_entries.find(id);
        if (it != m_entries.end()) {
            cb = std::move(it->second.onResult);
            m_entries.erase(it);
        }
        return cb;
    }

    // -------------------------------------------------------------------------
    // Data members
    // -------------------------------------------------------------------------
    mutable std::mutex                   m_mutex;
    std::unordered_map<uint32_t, Entry>  m_entries;
    std::atomic<uint32_t>                m_nextId{0};
    double                               m_clock{0.0};   // guarded by m_mutex
};
