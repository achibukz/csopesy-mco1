#pragma once

// Track 2 — Process & Instruction Engine (Person B)
//
// IInstruction interface + the six concrete command types + the randomized
// instruction generator and a factory adapter for the scheduler.
//
// Design notes:
//  * Atomic commands (PRINT/DECLARE/ADD/SUBTRACT/SLEEP) implement execute() —
//    a single side effect on the owning Process. They are the unit of work the
//    CPU advances by one per call to Process::executeNext().
//  * FOR is a *block*: it carries a body and a repeat count but performs no work
//    in execute(). The Process drives loops with an explicit execution stack
//    (see Process.cpp) so that a SLEEP nested inside a FOR still relinquishes the
//    CPU mid-loop, and so progress (current/total lines) stays accurate.

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class Process;
class IProcess;

// ---------------------------------------------------------------------------
// IInstruction
// ---------------------------------------------------------------------------
class IInstruction {
public:
    virtual ~IInstruction() = default;

    // Perform this instruction's atomic side effect on the process.
    // Called only by Process::executeNext(), which holds the process lock, so
    // implementations may freely use Process's mutation helpers.
    // Block instructions (FOR) leave this as a no-op; they are driven by the
    // Process execution stack via isBlock()/body()/repeats() instead.
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

// ---------------------------------------------------------------------------
// Concrete commands
// ---------------------------------------------------------------------------

// PRINT(msg [+ var]) — appends to the process's print log (never stdout).
// With no message and no variable, emits "Hello world from <process_name>!".
class PrintCommand : public IInstruction {
public:
    explicit PrintCommand(std::string msg = "", std::string varRef = "")
        : message_(std::move(msg)), varRef_(std::move(varRef)) {}
    void execute(Process& p) const override;

private:
    std::string message_;
    std::string varRef_;  // optional variable whose value is concatenated
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

// FOR([body], repeats) — repeat the body. Driven by the Process execution stack;
// execute() is intentionally a no-op. Max nesting depth (3) is enforced by the
// generator at creation time.
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

// ---------------------------------------------------------------------------
// Instruction generator
// ---------------------------------------------------------------------------
// Produces a Process whose top-level program holds between [minIns, maxIns]
// instructions, mixing all six types and respecting the FOR depth-<=3 rule.
class InstructionGenerator {
public:
    static constexpr int kMaxForDepth = 3;

    static std::unique_ptr<Process> generate(const std::string& name, int pid,
                                             uint32_t minIns, uint32_t maxIns);

private:
    // depthRemaining starts at kMaxForDepth; FOR generation decrements it and is
    // disallowed once it reaches 1 (so loops never nest deeper than kMaxForDepth).
    static std::unique_ptr<IInstruction> randomCommand(int depthRemaining);
};

// Factory adapter compatible with Scheduler::setProcessFactory(...).
// Track 3 wires this once, e.g. from Console::handleInitialize:
//     Scheduler::instance().setProcessFactory(
//         makeProcessFactory(cfg.minIns, cfg.maxIns));
std::function<std::unique_ptr<IProcess>(const std::string& name, int pid)>
makeProcessFactory(uint32_t minIns, uint32_t maxIns);
