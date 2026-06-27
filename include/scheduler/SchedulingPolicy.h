#pragma once

#include <cstdint>
#include <deque>

class IProcess;

// The ready queue is a deque so woken sleepers can be re-inserted at the front
// (see SchedulerEngine::tickSleepingProcesses); normal scheduling still treats
// it as FIFO: pickNext pops the front, onPreempt pushes the back.
class ISchedulingPolicy {
public:
    virtual ~ISchedulingPolicy() = default;

    virtual IProcess* pickNext(std::deque<IProcess*>& ready) = 0;
    virtual bool shouldKeepRunning(IProcess* p, int ticksOnCore, uint32_t quantum) = 0;
    virtual void onPreempt(IProcess* p, std::deque<IProcess*>& ready) = 0;
};

class FCFSPolicy : public ISchedulingPolicy {
public:
    IProcess* pickNext(std::deque<IProcess*>& ready) override;
    bool shouldKeepRunning(IProcess* p, int ticksOnCore, uint32_t quantum) override;
    void onPreempt(IProcess* p, std::deque<IProcess*>& ready) override;
};

class RRPolicy : public ISchedulingPolicy {
public:
    IProcess* pickNext(std::deque<IProcess*>& ready) override;
    bool shouldKeepRunning(IProcess* p, int ticksOnCore, uint32_t quantum) override;
    void onPreempt(IProcess* p, std::deque<IProcess*>& ready) override;
};
