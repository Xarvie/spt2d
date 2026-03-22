#pragma once

#include <unordered_map>
#include <functional>
#include <mutex>
#include <optional>
#include <vector>

template<typename ResultType>
class CallbackManager {
public:
    using Callback = std::function<void(const ResultType&)>;

    [[nodiscard]] int registerCallback(Callback cb, float timeoutSeconds = 30.0f) {
        std::lock_guard<std::mutex> lock(m_mutex);
        int id = ++m_nextId;
        m_callbacks.emplace(id, Entry{std::move(cb), m_currentTime + timeoutSeconds});
        return id;
    }

    void invokeAndRemove(int id, const ResultType& result) {
        Callback cb;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_callbacks.find(id);
            if (it == m_callbacks.end()) {
                return;
            }
            cb = std::move(it->second.func);
            m_callbacks.erase(it);
        }

        if (cb) {
            cb(result);
        }
    }

    void tick(float dt, const std::optional<ResultType>& timeoutResult = std::nullopt) {
        std::vector<Callback> expiredCallbacks;
        std::vector<ResultType> results;

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_currentTime += dt;

            for (auto it = m_callbacks.begin(); it != m_callbacks.end(); ) {
                if (m_currentTime >= it->second.expireTime) {
                    if (timeoutResult.has_value() && it->second.func) {
                        expiredCallbacks.push_back(std::move(it->second.func));
                        results.push_back(timeoutResult.value());
                    }
                    it = m_callbacks.erase(it);
                } else {
                    ++it;
                }
            }
        }

        for (size_t i = 0; i < expiredCallbacks.size(); ++i) {
            if (expiredCallbacks[i]) {
                expiredCallbacks[i](results[i]);
            }
        }
    }

    [[nodiscard]] size_t size() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_callbacks.size();
    }

    void clear() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_callbacks.clear();
    }

private:
    struct Entry {
        Callback func;
        float expireTime;
    };

    mutable std::mutex m_mutex;
    int m_nextId = 0;
    float m_currentTime = 0.0f;
    std::unordered_map<int, Entry> m_callbacks;
};
