#include "RawDataResource.h"

namespace spt3d {

RawDataResource::RawDataResource(const std::string& logicAddress, const std::string& physicalPath)
    : m_logicAddress(logicAddress)
    , m_physicalPath(physicalPath)
{
}

bool RawDataResource::loadFromMemory(const uint8_t* data, size_t size) {
    if (!data || size == 0) {
        m_state = ResState::Failed;
        return false;
    }

    m_data.assign(data, data + size);
    m_stringView.clear();
    m_state = ResState::Ready;
    return true;
}

bool RawDataResource::loadFromMemory(const std::vector<uint8_t>& data) {
    return loadFromMemory(data.data(), data.size());
}

const std::string& RawDataResource::asString() {
    if (m_stringView.empty() && !m_data.empty()) {
        m_stringView.assign(reinterpret_cast<const char*>(m_data.data()), m_data.size());
    }
    return m_stringView;
}

}
