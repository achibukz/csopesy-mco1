#pragma once

#include <cstdint>
#include <string>

enum class ProcessState { READY, RUNNING, SLEEPING, FINISHED };

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
};
