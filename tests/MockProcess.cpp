#include "MockProcess.h"

#include <iostream>
#include <mutex>

namespace {
std::mutex& stdoutMutex() {
    static std::mutex m;
    return m;
}
}  // namespace

MockProcess::MockProcess(int pid, std::string name, int totalInstructions,
                         std::optional<int> sleepAtLine, int sleepDuration,
                         bool verbose)
    : pid_(pid),
      name_(std::move(name)),
      total_(totalInstructions),
      sleepAtLine_(sleepAtLine),
      sleepDuration_(sleepDuration),
      verbose_(verbose) {}

int MockProcess::getCurrentLine() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return current_;
}

ProcessState MockProcess::getState() const {
    if (getStateHook) getStateHook();
    return state_.load();
}

void MockProcess::executeNext(uint64_t currentTick) {
    int recordedLine;
    bool triggerSleep = false;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (current_ >= total_) return;
        ++current_;
        recordedLine = current_;
        visits_.push_back({currentTick, current_});

        if (current_ >= total_) {
            state_.store(ProcessState::FINISHED);
        } else if (sleepAtLine_ && current_ == *sleepAtLine_ && sleepDuration_ > 0) {
            sleepRemaining_ = sleepDuration_;
            state_.store(ProcessState::SLEEPING);
            triggerSleep = true;
        } else {
            state_.store(ProcessState::RUNNING);
        }
    }
    if (verbose_) {
        std::lock_guard<std::mutex> lk(stdoutMutex());
        std::cout << "[tick=" << currentTick << " p=" << name_
                  << " line=" << recordedLine << "/" << total_
                  << (triggerSleep ? " (sleep)" : "")
                  << "]\n";
    }
}

void MockProcess::tickSleep() {
    if (tickSleepHook) tickSleepHook();
    std::lock_guard<std::mutex> lk(mtx_);
    if (state_.load() != ProcessState::SLEEPING) return;
    if (sleepRemaining_ > 0) --sleepRemaining_;
    if (sleepRemaining_ == 0) {
        state_.store(ProcessState::READY);
    }
}

std::vector<MockProcess::Visit> MockProcess::getVisits() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return visits_;
}
