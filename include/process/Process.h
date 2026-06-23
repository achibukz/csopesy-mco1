#pragma once

#include "process/Instructions.h"
#include "scheduler/IProcess.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

class Process : public IProcess {
public:
    Process(int pid, std::string name,
            std::vector<std::unique_ptr<IInstruction>> program);

    // IProcess
    int          getPID() const override         { return pid_; }
    std::string  getName() const override        { return name_; }
    ProcessState getState() const override        { return state_.load(); }   // lock-free
    int          getCurrentLine() const override { return currentLine_.load(); }  // lock-free
    int          getTotalLines() const override  { return totalInstructions_; }
    bool         isFinished() const override      { return state_.load() == ProcessState::FINISHED; }

    void executeNext(uint64_t currentTick) override;
    void tickSleep() override;

    // Read-only views for screen / process-smi / report-util
    std::vector<std::string>              getPrintLog() const override;  // returns copy
    std::chrono::system_clock::time_point getCreatedAt() const override { return createdAt_; }

    // Mutators for IInstruction::execute() — caller must hold mutex_
    uint16_t getVar(const std::string& name);          // auto-declares as 0 if missing
    void     setVar(const std::string& name, uint16_t value);
    void     appendPrint(const std::string& msg);
    void     requestSleep(uint8_t ticks);

private:
    // Entry on the execution stack: instruction list pointer, next index, remaining loop passes.
    struct Frame {
        const std::vector<std::unique_ptr<IInstruction>>* list;
        std::size_t                                       ip;
        int                                               loopsRemaining;
    };

    // Returns the next atomic instruction, or nullptr when the program is exhausted. Must hold mutex_.
    const IInstruction* nextAtomic();

    const int                                   pid_;
    const std::string                           name_;
    const std::vector<std::unique_ptr<IInstruction>> program_;
    const int                                   totalInstructions_;  // fully-expanded atomic count
    const std::chrono::system_clock::time_point createdAt_;

    std::atomic<ProcessState> state_{ProcessState::READY};
    std::atomic<int>          currentLine_{0};

    mutable std::mutex                            mutex_;
    std::unordered_map<std::string, uint16_t>     variables_;
    std::vector<std::string>                      printLog_;
    std::vector<Frame>                            execStack_;
    int                                           sleepRemaining_ = 0;
};
