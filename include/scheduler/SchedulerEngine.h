#pragma once

#include "config/Config.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

class IProcess;
class ISchedulingPolicy;

class SchedulerEngine {
public:
    using ProcessFactory =
        std::function<std::unique_ptr<IProcess>(const std::string& name, int pid)>;

    SchedulerEngine(SchedulerConfig cfg, ISchedulingPolicy& policy);
    ~SchedulerEngine();

    SchedulerEngine(const SchedulerEngine&) = delete;
    SchedulerEngine& operator=(const SchedulerEngine&) = delete;

    void start();
    void stop();
    void stepOnce();

    void      enqueueReady(IProcess* p);
    IProcess* popReady();
    void      markRunning(IProcess* p, int coreId);
    void      clearRunning(IProcess* p);
    void      markFinished(IProcess* p);
    void      preempt(IProcess* p, ISchedulingPolicy& policy);
    void      moveToSleeping(IProcess* p);  // removes from running, parks in sleeping pool
    void      tickSleepingProcesses();      // call once per tick; wakes READY ones back into ready_

    uint64_t currentTick() const { return tick_.load(); }
    void     waitForNextTick(uint64_t& lastSeen);

    void setProcessFactory(ProcessFactory f);
    void startGenerator();
    void stopGenerator();

    std::vector<IProcess*> snapshotRunning() const;
    std::vector<IProcess*> snapshotFinished() const;
    int    coresTotal() const { return cfg_.numCpu; }
    int    coresUsed() const;
    double cpuUtilization() const;

    const SchedulerConfig& config() const { return cfg_; }

    bool isRunning() const { return running_.load(); }

private:
    void tickLoop();
    void generatorLoop();
    void notifyTick();

    SchedulerConfig    cfg_;
    ISchedulingPolicy& policy_;

    std::atomic<uint64_t> tick_{0};
    std::atomic<bool>     running_{false};
    std::atomic<bool>     generatorRunning_{false};
    std::atomic<bool>     generatorEnabled_{false};

    mutable std::mutex      tickMutex_;
    std::condition_variable tickCv_;

    mutable std::mutex                     stateMutex_;
    std::queue<IProcess*>                  ready_;
    std::vector<IProcess*>                 runningProcs_;
    std::vector<IProcess*>                 sleepingProcs_;
    std::vector<IProcess*>                 finishedProcs_;
    std::vector<std::unique_ptr<IProcess>> owned_;
    int                                    coresUsed_ = 0;

    std::thread tickThread_;
    std::thread genThread_;

    ProcessFactory factory_;
    int            nextPid_ = 1;
};
