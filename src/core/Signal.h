#pragma once

#include <functional>
#include <vector>
#include <mutex>
#include <atomic>
#include <algorithm>

template<typename... Args>
class Signal {
public:
    using Slot = std::function<void(Args...)>;

    Signal() = default;
    ~Signal() = default;

    Signal(const Signal&) = delete;
    Signal& operator=(const Signal&) = delete;

    [[nodiscard]] uint32_t connect(Slot slot) {
        std::lock_guard<std::mutex> lock(m_mutex);
        uint32_t id = ++m_nextId;
        m_slots.push_back({id, std::move(slot), false});
        return id;
    }

    void disconnect(uint32_t id) {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& connection : m_slots) {
            if (connection.id == id) {
                connection.pendingRemove = true;
                m_isDirty = true;
                break;
            }
        }
    }

    void emit(Args... args) {
        std::vector<Connection> snapshot;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_isDirty) {
                cleanup();
            }
            snapshot = m_slots;
        }

        for (const auto& connection : snapshot) {
            if (!connection.pendingRemove && connection.slot) {
                connection.slot(args...);
            }
        }
    }

    void clear() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_slots.clear();
        m_isDirty = false;
    }

    [[nodiscard]] size_t size() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_slots.size();
    }

private:
    void cleanup() {
        m_slots.erase(
            std::remove_if(m_slots.begin(), m_slots.end(),
                [](const Connection& c) { return c.pendingRemove; }),
            m_slots.end()
        );
        m_isDirty = false;
    }

    struct Connection {
        uint32_t id;
        Slot slot;
        bool pendingRemove;
    };

    mutable std::mutex m_mutex;
    std::atomic<uint32_t> m_nextId{0};
    std::vector<Connection> m_slots;
    bool m_isDirty = false;
};
