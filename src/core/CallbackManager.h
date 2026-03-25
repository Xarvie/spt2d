#pragma once

#include <functional>
#include <map>
#include <mutex>
#include <cstdint>

namespace spt3d {

template<typename T>
class CallbackManager {
public:
    struct Handle {
        uint64_t value = 0;
        Handle() = default;
        explicit Handle(uint64_t v) : value(v) {}
        uint64_t getId() const { return value; }
        bool valid() const { return value != 0; }
    };

    Handle add(std::function<void(const T&)> callback, void* context = nullptr, float timeout = 0.0f) {
        std::lock_guard<std::mutex> lock(m_mutex);
        uint64_t id = ++m_nextId;
        CallbackEntry entry;
        entry.callback = std::move(callback);
        entry.context = context;
        entry.timeout = timeout;
        entry.elapsed = 0.0f;
        m_entries[id] = std::move(entry);
        return Handle(id);
    }

    void invoke(uint64_t id, const T& result) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_entries.find(id);
        if (it != m_entries.end()) {
            if (it->second.callback) {
                it->second.callback(result);
            }
            m_entries.erase(it);
        }
    }

    void tick(float dt) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_entries.begin();
        while (it != m_entries.end()) {
            if (it->second.timeout > 0.0f) {
                it->second.elapsed += dt;
                if (it->second.elapsed >= it->second.timeout) {
                    it = m_entries.erase(it);
                    continue;
                }
            }
            ++it;
        }
    }

    void clear() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_entries.clear();
    }

private:
    struct CallbackEntry {
        std::function<void(const T&)> callback;
        void* context = nullptr;
        float timeout = 0.0f;
        float elapsed = 0.0f;
    };

    std::mutex m_mutex;
    std::map<uint64_t, CallbackEntry> m_entries;
    uint64_t m_nextId = 0;
};

}
