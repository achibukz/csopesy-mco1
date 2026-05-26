#pragma once

#include "IProcess.h"

#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

class MockProcess : public IProcess {
public:
    struct Visit {
        uint64_t tick;
        int      line;
    };

    MockProcess(int pid, std::string name, int totalInstructions,
                std::optional<int> sleepAtLine = std::nullopt,
                int sleepDuration = 0,
                bool verbose = false);

    int           getPID() const override         { return pid_; }
    std::string   getName() const override        { return name_; }
    ProcessState  getState() const override       { return state_.load(); }
    int           getCurrentLine() const override;
    int           getTotalLines() const override  { return total_; }
    bool          isFinished() const override     { return state_.load() == ProcessState::FINISHED; }

    void executeNext(uint64_t currentTick) override;
    void tickSleep() override;

    std::vector<Visit> getVisits() const;

private:
    int                       pid_;
    std::string               name_;
    int                       total_;
    std::optional<int>        sleepAtLine_;
    int                       sleepDuration_;
    bool                      verbose_;

    mutable std::mutex        mtx_;
    int                       current_ = 0;
    int                       sleepRemaining_ = 0;
    std::atomic<ProcessState> state_{ProcessState::READY};
    std::vector<Visit>        visits_;
};
