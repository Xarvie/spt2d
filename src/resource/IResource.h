#pragma once

#include "ResHandle.h"
#include <atomic>
#include <string>
#include <functional>

namespace spt {

class IResource {
public:
    using GpuTaskDispatcher = std::function<void(std::function<void()>)>;

    virtual ~IResource() = default;

    virtual ResType getType() const = 0;
    virtual ResState getState() const = 0;
    virtual void setState(ResState state) = 0;

    void setResHandle(ResHandle h) { m_selfHandle = h; }
    ResHandle getResHandle() const { return m_selfHandle; }

    virtual const std::string& getLogicAddress() const = 0;
    virtual const std::string& getPhysicalPath() const = 0;

    virtual size_t getMemoryUsage() const { return 0; }

    virtual void addRef() {
        m_refCount.fetch_add(1, std::memory_order_relaxed);
    }

    virtual void release() {
        int32_t refs = m_refCount.fetch_sub(1, std::memory_order_acq_rel);
        if (refs == 1) {
            onRefCountZero();
        }
    }

    virtual int32_t getRefCount() const {
        return m_refCount.load(std::memory_order_relaxed);
    }

    virtual void cleanupGPU() {}
    virtual void cleanupCPU() {}

    void setGpuDispatcher(GpuTaskDispatcher dispatcher) {
        m_dispatcher = std::move(dispatcher);
    }

    void dispatchGpuTask(std::function<void()> task) {
        if (m_dispatcher) {
            m_dispatcher(std::move(task));
        }
    }

protected:
    IResource() : m_refCount(1) {}

    virtual void onRefCountZero() {
        cleanupCPU();
    }

private:
    GpuTaskDispatcher m_dispatcher;
    ResHandle m_selfHandle;
    std::atomic<int32_t> m_refCount;
};

}
