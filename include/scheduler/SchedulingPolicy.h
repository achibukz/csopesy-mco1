#pragma once

#include <cstdint>
#include <queue>

class IProcess;

class ISchedulingPolicy {
public:
    virtual ~ISchedulingPolicy() = default;

    virtual IProcess* pickNext(std::queue<IProcess*>& ready) = 0;
    virtual bool shouldKeepRunning(IProcess* p, int ticksOnCore, uint32_t quantum) = 0;
    virtual void onPreempt(IProcess* p, std::queue<IProcess*>& ready) = 0;
};

class FCFSPolicy : public ISchedulingPolicy {
public:
    IProcess* pickNext(std::queue<IProcess*>& ready) override;
    bool shouldKeepRunning(IProcess* p, int ticksOnCore, uint32_t quantum) override;
    void onPreempt(IProcess* p, std::queue<IProcess*>& ready) override;
};

class RRPolicy : public ISchedulingPolicy {
public:
    IProcess* pickNext(std::queue<IProcess*>& ready) override;
    bool shouldKeepRunning(IProcess* p, int ticksOnCore, uint32_t quantum) override;
    void onPreempt(IProcess* p, std::queue<IProcess*>& ready) override;
};
