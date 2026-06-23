#pragma once

// Track 2 — Process & Instruction Engine (Person B)
//
// Concrete Process that the scheduler drives through the IProcess seam.
// Holds the instruction program, the uint16 variable map, the print log, and
// the per-process state machine (READY/RUNNING/SLEEPING/FINISHED).
//
// Threading contract (mirrors the code-reference cheatsheet):
//  * getState()/getCurrentLine() read atomics -> lock-free for Track 1 / Track 4.
//  * getPrintLog() returns a copy under mutex_.
//  * variables_, printLog_, execStack_, sleepRemaining_ are guarded by mutex_.
//  * The instruction-facing mutators (getVar/setVar/appendPrint/requestSleep)
//    assume mutex_ is ALREADY held: they are only ever called from inside an
//    IInstruction::execute(), which runs inside executeNext() under the lock.

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

    // ---- IProcess (called by Track 1's CPU) ----
    int          getPID() const override         { return pid_; }
    std::string  getName() const override        { return name_; }
    ProcessState getState() const override        { return state_.load(); }
    int          getCurrentLine() const override { return currentLine_.load(); }
    int          getTotalLines() const override  { return totalInstructions_; }
    bool         isFinished() const override      { return state_.load() == ProcessState::FINISHED; }

    void executeNext(uint64_t currentTick) override;
    void tickSleep() override;

    // ---- Read-only views for Track 4 (screen / report) ----
    std::vector<std::string>              getPrintLog() const override;
    std::chrono::system_clock::time_point getCreatedAt() const override { return createdAt_; }

    // ---- Mutators used by IInstruction::execute() (lock already held) ----
    uint16_t getVar(const std::string& name);          // auto-declares as 0 if missing
    void     setVar(const std::string& name, uint16_t value);
    void     appendPrint(const std::string& msg);
    void     requestSleep(uint8_t ticks);

private:
    // One entry of the execution stack: a pointer into an instruction list,
    // the next index to run in it, and how many loop passes remain.
    struct Frame {
        const std::vector<std::unique_ptr<IInstruction>>* list;
        std::size_t                                       ip;
        int                                               loopsRemaining;
    };

    // Advance the execution stack to the next atomic instruction (or nullptr if
    // the program is exhausted). Must be called with mutex_ held.
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
