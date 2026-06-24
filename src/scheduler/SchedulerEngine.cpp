#include "scheduler/SchedulerEngine.h"

#include "scheduler/IProcess.h"
#include "scheduler/SchedulingPolicy.h"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <unordered_set>
#include <utility>

namespace {

std::string makeProcessName(int pid) {
    std::ostringstream oss;
    oss << "p" << std::setw(2) << std::setfill('0') << pid;
    return oss.str();
}

}  // namespace

SchedulerEngine::SchedulerEngine(SchedulerConfig cfg, ISchedulingPolicy& policy)
    : cfg_(std::move(cfg)), policy_(policy) {
    runningProcs_.assign(cfg_.numCpu, nullptr);
}

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
    if (generatorEnabled_.load() && tick_.load() % cfg_.batchProcessFreq == 0) {
        generateNextReadyProcess();
    }
}

void SchedulerEngine::tickLoop() {
    while (running_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
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

        for (uint64_t t = lastSeen + 1; t <= current; ++t) {
            if (!generatorEnabled_.load()) break;
            if (t % cfg_.batchProcessFreq == 0) {
                generateNextReadyProcess();
            }
        }
        lastSeen = current;
    }
}

bool SchedulerEngine::generateNextReadyProcess() {
    if (!factory_) return false;

    int pid;
    {
        std::lock_guard<std::mutex> lk(stateMutex_);
        pid = nextPid_++;
    }

    auto proc = factory_(makeProcessName(pid), pid);
    if (!proc) return false;

    IProcess* raw = proc.get();
    {
        std::lock_guard<std::mutex> lk(stateMutex_);
        owned_.push_back(std::move(proc));
        ready_.push(raw);
    }
    return true;
}

void SchedulerEngine::enqueueReady(IProcess* p) {
    if (!p) return;
    std::lock_guard<std::mutex> lk(stateMutex_);
    ready_.push(p);
}

IProcess* SchedulerEngine::createReadyProcess(const std::string& name) {
    if (!factory_) return nullptr;

    int pid;
    {
        std::lock_guard<std::mutex> lk(stateMutex_);
        pid = nextPid_++;
    }

    auto proc = factory_(name, pid);
    if (!proc) return nullptr;

    IProcess* raw = proc.get();
    {
        std::lock_guard<std::mutex> lk(stateMutex_);
        owned_.push_back(std::move(proc));
        ready_.push(raw);
    }
    return raw;
}

IProcess* SchedulerEngine::popReady() {
    std::lock_guard<std::mutex> lk(stateMutex_);
    return policy_.pickNext(ready_);
}

void SchedulerEngine::markRunning(IProcess* p, int coreId) {
    if (!p) return;
    if (coreId < 0 || coreId >= static_cast<int>(runningProcs_.size())) return;
    std::lock_guard<std::mutex> lk(stateMutex_);
    if (runningProcs_[coreId] == p) return;
    if (runningProcs_[coreId] == nullptr) {
        ++coresUsed_;
    }
    runningProcs_[coreId] = p;
}

void SchedulerEngine::clearRunning(IProcess* p) {
    if (!p) return;
    std::lock_guard<std::mutex> lk(stateMutex_);
    for (auto& slot : runningProcs_) {
        if (slot == p) {
            slot = nullptr;
            --coresUsed_;
            return;
        }
    }
}

void SchedulerEngine::markFinished(IProcess* p) {
    if (!p) return;
    std::lock_guard<std::mutex> lk(stateMutex_);
    for (auto& slot : runningProcs_) {
        if (slot == p) {
            slot = nullptr;
            --coresUsed_;
            break;
        }
    }
    finishedProcs_.push_back(p);
}

void SchedulerEngine::preempt(IProcess* p, ISchedulingPolicy& policy) {
    if (!p) return;
    std::lock_guard<std::mutex> lk(stateMutex_);
    for (auto& slot : runningProcs_) {
        if (slot == p) {
            slot = nullptr;
            --coresUsed_;
            break;
        }
    }
    policy.onPreempt(p, ready_);
}

void SchedulerEngine::moveToSleeping(IProcess* p) {
    if (!p) return;
    std::lock_guard<std::mutex> lk(stateMutex_);
    for (auto& slot : runningProcs_) {
        if (slot == p) {
            slot = nullptr;
            --coresUsed_;
            break;
        }
    }
    if (std::find(sleepingProcs_.begin(), sleepingProcs_.end(), p) == sleepingProcs_.end()) {
        sleepingProcs_.push_back(p);
    }
}

void SchedulerEngine::adoptSleeping(std::unique_ptr<IProcess> p) {
    if (!p) return;
    std::lock_guard<std::mutex> lk(stateMutex_);
    IProcess* raw = p.get();
    owned_.push_back(std::move(p));
    sleepingProcs_.push_back(raw);
}

void SchedulerEngine::tickSleepingProcesses() {
    // DS line 422: never hold stateMutex_ across a process method call.
    // 1) snapshot sleepers under lock
    std::vector<IProcess*> sleepers;
    {
        std::lock_guard<std::mutex> lk(stateMutex_);
        sleepers = sleepingProcs_;
    }

    // 2) tick each one with no lock held
    for (IProcess* p : sleepers) {
        p->tickSleep();
    }

    // 3) read post-tick states off-lock; collect woken pointers
    std::unordered_set<IProcess*> woken;
    for (IProcess* p : sleepers) {
        if (p->getState() != ProcessState::WAITING) {
            woken.insert(p);
        }
    }

    // 4) under lock, rebuild sleepingProcs_ from the engine's *current* list
    //    (preserving any sleepers added between steps 1 and 4) using the
    //    off-lock `woken` set as a pure-data predicate. No process method
    //    is called inside this critical section.
    std::lock_guard<std::mutex> lk(stateMutex_);
    std::vector<IProcess*> stillSleeping;
    stillSleeping.reserve(sleepingProcs_.size());
    for (IProcess* p : sleepingProcs_) {
        if (woken.count(p)) {
            ready_.push(p);
        } else {
            stillSleeping.push_back(p);
        }
    }
    sleepingProcs_.swap(stillSleeping);
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

std::vector<IProcess*> SchedulerEngine::snapshotAll() const {
    std::lock_guard<std::mutex> lk(stateMutex_);
    std::vector<IProcess*> out;
    out.reserve(owned_.size());
    for (const auto& p : owned_) {
        out.push_back(p.get());
    }
    return out;
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
