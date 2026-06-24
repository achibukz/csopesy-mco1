#include "scheduler/SchedulingPolicy.h"

#include "scheduler/IProcess.h"

namespace {

IProcess* popFront(std::queue<IProcess*>& q) {
    if (q.empty()) return nullptr;
    IProcess* p = q.front();
    q.pop();
    return p;
}

}  // namespace

IProcess* FCFSPolicy::pickNext(std::queue<IProcess*>& ready) {
    return popFront(ready);
}

bool FCFSPolicy::shouldKeepRunning(IProcess* p, int, uint32_t) {
    if (!p) return false;
    return p->getState() != ProcessState::FINISHED &&
           p->getState() != ProcessState::WAITING;
}

void FCFSPolicy::onPreempt(IProcess*, std::queue<IProcess*>&) {
    // FCFS is run-to-completion. Only invoked when sleeping/finished.
}

IProcess* RRPolicy::pickNext(std::queue<IProcess*>& ready) {
    return popFront(ready);
}

bool RRPolicy::shouldKeepRunning(IProcess* p, int ticksOnCore, uint32_t quantum) {
    if (!p) return false;
    if (p->getState() == ProcessState::FINISHED) return false;
    if (p->getState() == ProcessState::WAITING) return false;
    if (quantum == 0) return true;
    return static_cast<uint32_t>(ticksOnCore) < quantum;
}

void RRPolicy::onPreempt(IProcess* p, std::queue<IProcess*>& ready) {
    if (!p) return;
    if (p->getState() == ProcessState::FINISHED) return;
    ready.push(p);
}
