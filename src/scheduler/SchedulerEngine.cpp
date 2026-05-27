#include "scheduler/SchedulerEngine.h"

#include "scheduler/IProcess.h"
#include "scheduler/SchedulingPolicy.h"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <utility>

namespace {

std::string makeProcessName(int pid) {
    std::ostringstream oss;
    oss << "p" << std::setw(2) << std::setfill('0') << pid;
    return oss.str();
}

}  // namespace

SchedulerEngine::SchedulerEngine(SchedulerConfig cfg, ISchedulingPolicy& policy)
    : cfg_(std::move(cfg)), policy_(policy) {}

SchedulerEngine::~SchedulerEngine() {
    stop();
}

void SchedulerEngine::start() {
    if (running_.exchange(true)) return;
    tickThread_ = std::thread(&SchedulerEngine::tickLoop, this);
    generatorRunning_.store(true);
    genThread_ = std::thread(&SchedulerEngine::generatorLoop, this);
}

void SchedulerEngine::stop() {
    if (!running_.exchange(false)) return;
    generatorRunning_.store(false);
    generatorEnabled_.store(false);

    {
        std::lock_guard<std::mutex> lk(tickMutex_);
        tickCv_.notify_all();
    }

    if (tickThread_.joinable()) tickThread_.join();
    if (genThread_.joinable())  genThread_.join();
}

void SchedulerEngine::notifyTick() {
    std::lock_guard<std::mutex> lk(tickMutex_);
    tickCv_.notify_all();
}

void SchedulerEngine::stepOnce() {
    tick_.fetch_add(1);
    tickSleepingProcesses();
    notifyTick();
    if (generatorEnabled_.load()) {
        if (factory_ && tick_.load() % cfg_.batchProcessFreq == 0) {
            int pid;
            {
                std::lock_guard<std::mutex> lk(stateMutex_);
                pid = nextPid_++;
            }
            auto proc = factory_(makeProcessName(pid), pid);
            if (proc) {
                IProcess* raw = proc.get();
                {
                    std::lock_guard<std::mutex> lk(stateMutex_);
                    owned_.push_back(std::move(proc));
                    ready_.push(raw);
                }
            }
        }
    }
}

void SchedulerEngine::tickLoop() {
    while (running_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if (!running_.load()) break;
        tick_.fetch_add(1);
        tickSleepingProcesses();
        notifyTick();
    }
}

void SchedulerEngine::generatorLoop() {
    uint64_t lastSeen = tick_.load();
    while (generatorRunning_.load() && running_.load()) {
        {
            std::unique_lock<std::mutex> lk(tickMutex_);
            tickCv_.wait_for(lk, std::chrono::milliseconds(5), [&] {
                return !generatorRunning_.load() || !running_.load() ||
                       tick_.load() != lastSeen;
            });
        }
        if (!generatorRunning_.load() || !running_.load()) break;
        uint64_t current = tick_.load();
        if (current == lastSeen) continue;
        lastSeen = current;

        if (!generatorEnabled_.load()) continue;
        if (!factory_) continue;
        if (current % cfg_.batchProcessFreq != 0) continue;

        int pid;
        {
            std::lock_guard<std::mutex> lk(stateMutex_);
            pid = nextPid_++;
        }
        auto proc = factory_(makeProcessName(pid), pid);
        if (!proc) continue;
        IProcess* raw = proc.get();
        std::lock_guard<std::mutex> lk(stateMutex_);
        owned_.push_back(std::move(proc));
        ready_.push(raw);
    }
}

void SchedulerEngine::enqueueReady(IProcess* p) {
    if (!p) return;
    std::lock_guard<std::mutex> lk(stateMutex_);
    ready_.push(p);
}

IProcess* SchedulerEngine::popReady() {
    std::lock_guard<std::mutex> lk(stateMutex_);
    return policy_.pickNext(ready_);
}

void SchedulerEngine::markRunning(IProcess* p, int /*coreId*/) {
    if (!p) return;
    std::lock_guard<std::mutex> lk(stateMutex_);
    if (std::find(runningProcs_.begin(), runningProcs_.end(), p) == runningProcs_.end()) {
        runningProcs_.push_back(p);
        ++coresUsed_;
    }
}

void SchedulerEngine::clearRunning(IProcess* p) {
    if (!p) return;
    std::lock_guard<std::mutex> lk(stateMutex_);
    auto it = std::find(runningProcs_.begin(), runningProcs_.end(), p);
    if (it != runningProcs_.end()) {
        runningProcs_.erase(it);
        --coresUsed_;
    }
}

void SchedulerEngine::markFinished(IProcess* p) {
    if (!p) return;
    std::lock_guard<std::mutex> lk(stateMutex_);
    auto it = std::find(runningProcs_.begin(), runningProcs_.end(), p);
    if (it != runningProcs_.end()) {
        runningProcs_.erase(it);
        --coresUsed_;
    }
    finishedProcs_.push_back(p);
}

void SchedulerEngine::preempt(IProcess* p, ISchedulingPolicy& policy) {
    if (!p) return;
    std::lock_guard<std::mutex> lk(stateMutex_);
    auto it = std::find(runningProcs_.begin(), runningProcs_.end(), p);
    if (it != runningProcs_.end()) {
        runningProcs_.erase(it);
        --coresUsed_;
    }
    policy.onPreempt(p, ready_);
}

void SchedulerEngine::moveToSleeping(IProcess* p) {
    if (!p) return;
    std::lock_guard<std::mutex> lk(stateMutex_);
    auto it = std::find(runningProcs_.begin(), runningProcs_.end(), p);
    if (it != runningProcs_.end()) {
        runningProcs_.erase(it);
        --coresUsed_;
    }
    if (std::find(sleepingProcs_.begin(), sleepingProcs_.end(), p) == sleepingProcs_.end()) {
        sleepingProcs_.push_back(p);
    }
}

void SchedulerEngine::tickSleepingProcesses() {
    std::vector<IProcess*> woken;
    {
        std::lock_guard<std::mutex> lk(stateMutex_);
        auto it = sleepingProcs_.begin();
        while (it != sleepingProcs_.end()) {
            IProcess* p = *it;
            p->tickSleep();
            if (p->getState() != ProcessState::SLEEPING) {
                woken.push_back(p);
                it = sleepingProcs_.erase(it);
            } else {
                ++it;
            }
        }
        for (IProcess* p : woken) ready_.push(p);
    }
}

void SchedulerEngine::waitForNextTick(uint64_t& lastSeen) {
    std::unique_lock<std::mutex> lk(tickMutex_);
    tickCv_.wait_for(lk, std::chrono::milliseconds(20), [&] {
        return !running_.load() || tick_.load() != lastSeen;
    });
    lastSeen = tick_.load();
}

void SchedulerEngine::setProcessFactory(ProcessFactory f) {
    factory_ = std::move(f);
}

void SchedulerEngine::startGenerator() {
    generatorEnabled_.store(true);
}

void SchedulerEngine::stopGenerator() {
    generatorEnabled_.store(false);
}

std::vector<IProcess*> SchedulerEngine::snapshotRunning() const {
    std::lock_guard<std::mutex> lk(stateMutex_);
    return runningProcs_;
}

std::vector<IProcess*> SchedulerEngine::snapshotFinished() const {
    std::lock_guard<std::mutex> lk(stateMutex_);
    return finishedProcs_;
}

int SchedulerEngine::coresUsed() const {
    std::lock_guard<std::mutex> lk(stateMutex_);
    return coresUsed_;
}

double SchedulerEngine::cpuUtilization() const {
    int total = coresTotal();
    if (total <= 0) return 0.0;
    int used = coresUsed();
    return (100.0 * used) / total;
}
