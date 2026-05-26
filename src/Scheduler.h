#pragma once

#include "Config.h"
#include "SchedulerEngine.h"

#include <memory>
#include <vector>

class IProcess;
class ISchedulingPolicy;
class CPU;

class Scheduler {
public:
    static Scheduler& instance();

    void initialize(const SchedulerConfig& cfg);
    void shutdown();
    bool isInitialized() const { return initialized_; }

    void enqueue(IProcess* p);
    void setProcessFactory(SchedulerEngine::ProcessFactory f);

    void startGenerator();
    void stopGenerator();

    double                 getCpuUtilization() const;
    int                    getCoresUsed() const;
    int                    getCoresTotal() const;
    std::vector<IProcess*> getRunningSnapshot() const;
    std::vector<IProcess*> getFinishedSnapshot() const;
    uint64_t               getCurrentTick() const;

    SchedulerEngine& engineForTesting();

private:
    Scheduler() = default;
    Scheduler(const Scheduler&) = delete;
    Scheduler& operator=(const Scheduler&) = delete;

    std::unique_ptr<ISchedulingPolicy> policy_;
    std::unique_ptr<SchedulerEngine>   engine_;
    std::vector<std::unique_ptr<CPU>>  cores_;
    bool initialized_ = false;
};
