#include "scheduler/CPU.h"

#include "scheduler/IProcess.h"
#include "scheduler/SchedulerEngine.h"
#include "scheduler/SchedulingPolicy.h"

CPU::CPU(int coreId, SchedulerEngine& engine, ISchedulingPolicy& policy,
         uint32_t quantum, uint32_t delaysPerExec)
    : coreId_(coreId), engine_(engine), policy_(policy),
      quantum_(quantum), delaysPerExec_(delaysPerExec) {}

CPU::~CPU() {
    stop();
}

void CPU::start() {
    if (running_.exchange(true)) return;
    lastSeenTick_ = engine_.currentTick();
    thread_ = std::thread(&CPU::run, this);
}

void CPU::stop() {
    if (!running_.exchange(false)) return;
    if (thread_.joinable()) thread_.join();
}

void CPU::run() {
    while (running_.load()) {
        engine_.waitForNextTick(lastSeenTick_);
        if (!running_.load()) break;
        if (!engine_.isRunning()) break;

        IProcess* p = current_.load();

        if (!p) {
            p = engine_.popReady();
            if (!p) continue;
            engine_.markRunning(p, coreId_);
            current_.store(p);
            ticksOnCurrent_ = 0;
            delayTicksRemaining_ = 0;
        }

        if (delayTicksRemaining_ > 0) {
            --delayTicksRemaining_;
            ++ticksOnCurrent_;
            continue;
        }

        p->executeNext(engine_.currentTick());
        delayTicksRemaining_ = delaysPerExec_;
        ++ticksOnCurrent_;

        bool releasedCore = false;

        if (p->isFinished()) {
            engine_.markFinished(p);
            current_.store(nullptr);
            releasedCore = true;
        } else if (p->getState() == ProcessState::WAITING) {
            engine_.moveToSleeping(p);
            current_.store(nullptr);
            releasedCore = true;
        } else if (!policy_.shouldKeepRunning(p, ticksOnCurrent_, quantum_)) {
            engine_.preempt(p, policy_);
            current_.store(nullptr);
            releasedCore = true;
        }

        if (releasedCore) {
            IProcess* q = engine_.popReady();
            if (q) {
                engine_.markRunning(q, coreId_);
                current_.store(q);
                ticksOnCurrent_ = 0;
                delayTicksRemaining_ = 0;
            }
        }
    }
}
