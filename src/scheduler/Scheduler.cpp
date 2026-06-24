#include "scheduler/Scheduler.h"

#include "scheduler/CPU.h"
#include "scheduler/IProcess.h"
#include "scheduler/SchedulingPolicy.h"

Scheduler& Scheduler::instance() {
    static Scheduler s;
    return s;
}

void Scheduler::initialize(const SchedulerConfig& cfg) {
    if (initialized_) shutdown();

    if (cfg.algo == SchedulerConfig::Algo::FCFS) {
        policy_ = std::make_unique<FCFSPolicy>();
    } else {
        policy_ = std::make_unique<RRPolicy>();
    }

    engine_ = std::make_unique<SchedulerEngine>(cfg, *policy_);
    engine_->start();

    cores_.reserve(cfg.numCpu);
    for (int i = 0; i < cfg.numCpu; ++i) {
        auto core = std::make_unique<CPU>(i, *engine_, *policy_, cfg.quantum, cfg.delaysPerExec);
        core->start();
        cores_.push_back(std::move(core));
    }

    initialized_ = true;
}

void Scheduler::shutdown() {
    if (!initialized_) return;
    if (engine_) engine_->stopGenerator();
    for (auto& c : cores_) c->stop();
    cores_.clear();
    if (engine_) engine_->stop();
    engine_.reset();
    policy_.reset();
    initialized_ = false;
}

void Scheduler::enqueue(IProcess* p) {
    if (!engine_) return;
    engine_->enqueueReady(p);
}

IProcess* Scheduler::createProcess(const std::string& name) {
    return engine_ ? engine_->createReadyProcess(name) : nullptr;
}

void Scheduler::setProcessFactory(SchedulerEngine::ProcessFactory f) {
    if (!engine_) return;
    engine_->setProcessFactory(std::move(f));
}

void Scheduler::startGenerator() {
    if (engine_) engine_->startGenerator();
}

void Scheduler::stopGenerator() {
    if (engine_) engine_->stopGenerator();
}

double Scheduler::getCpuUtilization() const {
    return engine_ ? engine_->cpuUtilization() : 0.0;
}

int Scheduler::getCoresUsed() const {
    return engine_ ? engine_->coresUsed() : 0;
}

int Scheduler::getCoresTotal() const {
    return engine_ ? engine_->coresTotal() : 0;
}

std::vector<IProcess*> Scheduler::getRunningSnapshot() const {
    return engine_ ? engine_->snapshotRunning() : std::vector<IProcess*>{};
}

std::vector<IProcess*> Scheduler::getFinishedSnapshot() const {
    return engine_ ? engine_->snapshotFinished() : std::vector<IProcess*>{};
}

std::vector<IProcess*> Scheduler::getAllSnapshot() const {
    return engine_ ? engine_->snapshotAll() : std::vector<IProcess*>{};
}

uint64_t Scheduler::getCurrentTick() const {
    return engine_ ? engine_->currentTick() : 0;
}

SchedulerEngine& Scheduler::engineForTesting() {
    return *engine_;
}
