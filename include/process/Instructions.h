#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class Process;
class IProcess;

class IInstruction {
public:
    virtual ~IInstruction() = default;

    // Called by Process::executeNext() under the process lock.
    // FOR leaves this as a no-op; the Process execution stack drives it instead.
    virtual void execute(Process& p) const { (void)p; }

    virtual bool isBlock() const { return false; }
    virtual int  repeats() const { return 0; }
    virtual const std::vector<std::unique_ptr<IInstruction>>& body() const;
};

// Operand for ADD / SUBTRACT — either a literal or a variable reference.
struct Operand {
    bool        isLiteral = true;
    uint16_t    literal   = 0;
    std::string varName;

    static Operand lit(uint16_t v)            { return Operand{true, v, ""}; }
    static Operand var(std::string name)      { return Operand{false, 0, std::move(name)}; }
};

// PRINT(msg [+ var]) — appends to the print log. Defaults to "Hello world from <name>!".
class PrintCommand : public IInstruction {
public:
    explicit PrintCommand(std::string msg = "", std::string varRef = "")
        : message_(std::move(msg)), varRef_(std::move(varRef)) {}
    void execute(Process& p) const override;

private:
    std::string message_;
    std::string varRef_;  // if set, its value is appended to message_
};

// DECLARE(var, value) — initialize a uint16 variable.
class DeclareCommand : public IInstruction {
public:
    DeclareCommand(std::string var, uint16_t value)
        : varName_(std::move(var)), value_(value) {}
    void execute(Process& p) const override;

private:
    std::string varName_;
    uint16_t    value_;
};

// ADD(target, op2, op3) — target = op2 + op3, clamped to [0, 65535].
class AddCommand : public IInstruction {
public:
    AddCommand(std::string target, Operand op2, Operand op3)
        : target_(std::move(target)), op2_(std::move(op2)), op3_(std::move(op3)) {}
    void execute(Process& p) const override;

private:
    std::string target_;
    Operand     op2_, op3_;
};

// SUBTRACT(target, op2, op3) — target = op2 - op3, clamped to [0, 65535].
class SubtractCommand : public IInstruction {
public:
    SubtractCommand(std::string target, Operand op2, Operand op3)
        : target_(std::move(target)), op2_(std::move(op2)), op3_(std::move(op3)) {}
    void execute(Process& p) const override;

private:
    std::string target_;
    Operand     op2_, op3_;
};

// SLEEP(ticks) — relinquish the CPU for the given number of ticks.
class SleepCommand : public IInstruction {
public:
    explicit SleepCommand(uint8_t ticks) : ticks_(ticks) {}
    void execute(Process& p) const override;

private:
    uint8_t ticks_;
};

// FOR([body], repeats) — execute() is a no-op; the Process execution stack drives it.
// Generator enforces max nesting depth of 3.
class ForCommand : public IInstruction {
public:
    ForCommand(std::vector<std::unique_ptr<IInstruction>> body, int repeats)
        : body_(std::move(body)), repeats_(repeats) {}

    bool isBlock() const override { return true; }
    int  repeats() const override { return repeats_; }
    const std::vector<std::unique_ptr<IInstruction>>& body() const override { return body_; }

private:
    std::vector<std::unique_ptr<IInstruction>> body_;
    int                                        repeats_;
};

// Produces a randomized Process with [minIns, maxIns] top-level instructions
// mixing all six types; FOR nesting is capped at kMaxForDepth.
class InstructionGenerator {
public:
    static constexpr int kMaxForDepth = 3;

    static std::unique_ptr<Process> generate(const std::string& name, int pid,
                                             uint32_t minIns, uint32_t maxIns);

private:
    // depthRemaining decrements on each FOR recursion; FOR is disallowed once it hits 1.
    static std::unique_ptr<IInstruction> randomCommand(int depthRemaining);
};

// Factory adapter for Scheduler::setProcessFactory(); wired in Console::handleInitialize.
std::function<std::unique_ptr<IProcess>(const std::string& name, int pid)>
makeProcessFactory(uint32_t minIns, uint32_t maxIns);
