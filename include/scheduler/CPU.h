#pragma once

#include <atomic>
#include <cstdint>
#include <thread>

class IProcess;
class ISchedulingPolicy;
class SchedulerEngine;

class CPU {
public:
    CPU(int coreId, SchedulerEngine& engine, ISchedulingPolicy& policy,
        uint32_t quantum, uint32_t delaysPerExec);
    ~CPU();

    CPU(const CPU&) = delete;
    CPU& operator=(const CPU&) = delete;

    void start();
    void stop();

    int       getCoreId() const         { return coreId_; }
    bool      isBusy() const            { return current_.load() != nullptr; }
    IProcess* getCurrentProcess() const { return current_.load(); }

private:
    void run();

    int                    coreId_;
    SchedulerEngine&       engine_;
    ISchedulingPolicy&     policy_;
    uint32_t               quantum_;
    uint32_t               delaysPerExec_;

    std::atomic<IProcess*> current_{nullptr};
    std::atomic<bool>      running_{false};
    std::thread            thread_;

    int                    ticksOnCurrent_ = 0;
    uint64_t               delayTicksRemaining_ = 0;
    uint64_t               lastSeenTick_ = 0;
};
