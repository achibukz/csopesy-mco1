#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

enum class ProcessState { READY, RUNNING, WAITING, FINISHED };

class IProcess {
public:
    virtual ~IProcess() = default;

    virtual int           getPID() const         = 0;
    virtual std::string   getName() const        = 0;
    virtual ProcessState  getState() const       = 0;
    virtual int           getCurrentLine() const = 0;
    virtual int           getTotalLines() const  = 0;
    virtual bool          isFinished() const     = 0;

    virtual void executeNext(uint64_t currentTick) = 0;
    virtual void tickSleep()                       = 0;

    // Read-only views consumed by Track 4 (screen / process-smi / report-util)
    // through the engine's IProcess* snapshots. Implementations must return
    // copies / immutable values so no mutable state escapes.
    virtual std::vector<std::string>              getPrintLog() const = 0;
    virtual std::chrono::system_clock::time_point getCreatedAt() const = 0;
};
