#pragma once

#include "ThreadConfig.h"
#include "ThreadSafeQueue.h"
#include "DoubleBuffer.h"
#include "GameWork.h"
#include "../platform/Platform.h"

#include <functional>
#include <vector>
#include <memory>

namespace spt {

using TouchEventQueue = ThreadSafeQueue<TouchEvent>;
using KeyEventQueue = ThreadSafeQueue<KeyEvent>;
using MouseEventQueue = ThreadSafeQueue<MouseEvent>;
using GpuTaskQueue = ThreadSafeQueue<GpuTask>;

class IGameLogic {
public:
    virtual ~IGameLogic() = default;

    virtual bool onInit() = 0;
    virtual void onUpdate(float dt, 
                          const std::vector<TouchEvent>& touches,
                          const std::vector<KeyEvent>& keys,
                          const std::vector<MouseEvent>& mice) = 0;
    virtual void onRender(GameWork& work) = 0;
    virtual void onShutdown() = 0;
    virtual bool isRunning() const = 0;
};

class ThreadModel {
public:
    virtual ~ThreadModel() = default;

    virtual bool initialize(const ThreadConfig& config, std::unique_ptr<IGameLogic> logic) = 0;
    virtual void shutdown() = 0;

    virtual void pushTouchEvent(const TouchEvent& event) = 0;
    virtual void pushKeyEvent(const KeyEvent& event) = 0;
    virtual void pushMouseEvent(const MouseEvent& event) = 0;

    virtual void onFrameBegin() = 0;
    virtual GameWork* getRenderWork() = 0;
    virtual void onFrameEnd() = 0;

    virtual bool isRunning() const = 0;
    virtual bool isMultithreaded() const = 0;

    virtual size_t pendingGpuTasks() const = 0;
    virtual void executeGpuTasks() = 0;

    virtual const ThreadConfig& config() const = 0;

    static std::unique_ptr<ThreadModel> create(const ThreadConfig& config);
};

}
