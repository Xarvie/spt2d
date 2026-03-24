#pragma once

#include "../resource/IResource.h"
#include <atomic>

namespace spt {

class RawDataResource : public IResource {
public:
    static constexpr ResType StaticType = ResType::RawData;

    RawDataResource(const std::string& logicAddress, const std::string& physicalPath);
    ~RawDataResource() override = default;

    ResType getType() const override { return ResType::RawData; }
    ResState getState() const override { return m_state.load(std::memory_order_relaxed); }
    void setState(ResState state) override { m_state.store(state, std::memory_order_relaxed); }

    const std::string& getLogicAddress() const override { return m_logicAddress; }
    const std::string& getPhysicalPath() const override { return m_physicalPath; }

    size_t getMemoryUsage() const override { return m_data.size(); }

    void cleanupCPU() override {
        m_data.clear();
        m_data.shrink_to_fit();
        m_stringView.clear();
        m_state.store(ResState::Unloaded, std::memory_order_relaxed);
    }

    void setData(std::vector<uint8_t> data) {
        m_data = std::move(data);
        m_stringView.clear();
        m_state.store(ResState::Ready, std::memory_order_relaxed);
    }

    bool loadFromMemory(const uint8_t* data, size_t size);
    bool loadFromMemory(const std::vector<uint8_t>& data);

    const std::vector<uint8_t>& getData() const { return m_data; }
    size_t getSize() const { return m_data.size(); }
    const uint8_t* getPtr() const { return m_data.data(); }

    const std::string& asString();

private:
    std::string m_logicAddress;
    std::string m_physicalPath;
    std::atomic<ResState> m_state{ResState::Unloaded};
    std::vector<uint8_t> m_data;
    std::string m_stringView;
};

}
