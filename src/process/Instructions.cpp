#include "process/Instructions.h"

#include "process/Process.h"
#include "scheduler/IProcess.h"

#include <algorithm>
#include <random>
#include <string>

// Shared empty list returned by non-block instructions.
const std::vector<std::unique_ptr<IInstruction>>& IInstruction::body() const {
    static const std::vector<std::unique_ptr<IInstruction>> kEmpty;
    return kEmpty;
}

void PrintCommand::execute(Process& p) const {
    if (message_.empty() && varRef_.empty()) {
        p.appendPrint("Hello world from " + p.getName() + "!");
        return;
    }
    std::string out = message_;
    if (!varRef_.empty()) out += std::to_string(p.getVar(varRef_));
    p.appendPrint(out);
}

void DeclareCommand::execute(Process& p) const {
    p.setVar(varName_, value_);
}

namespace {
uint16_t resolve(Process& p, const Operand& op) {
    return op.isLiteral ? op.literal : p.getVar(op.varName);
}
}  // namespace

void AddCommand::execute(Process& p) const {
    uint32_t a = resolve(p, op2_);
    uint32_t b = resolve(p, op3_);
    uint32_t result = a + b;
    if (result > 65535u) result = 65535u;  // clamp, not wrap
    p.setVar(target_, static_cast<uint16_t>(result));
}

void SubtractCommand::execute(Process& p) const {
    int32_t a = resolve(p, op2_);
    int32_t b = resolve(p, op3_);
    int32_t result = a - b;
    if (result < 0) result = 0;              // floor at 0 (no underflow wrap)
    if (result > 65535) result = 65535;
    p.setVar(target_, static_cast<uint16_t>(result));
}

void SleepCommand::execute(Process& p) const {
    p.requestSleep(ticks_);
}

// ForCommand has no atomic side effect — the Process execution stack drives it.

namespace {

std::mt19937& rng() {
    // Per-thread so the scheduler generator thread and console (screen -s) don't race.
    static thread_local std::mt19937 engine{std::random_device{}()};
    return engine;
}

int randInt(int lo, int hi) {
    std::uniform_int_distribution<int> d(lo, hi);
    return d(rng());
}

}  // namespace

std::unique_ptr<Process> InstructionGenerator::generate(const std::string& name, int pid,
                                                        uint32_t minIns, uint32_t maxIns) {
    if (maxIns < minIns) std::swap(minIns, maxIns);
    if (minIns == 0) minIns = 1;  // never generate a zero-length program
    if (maxIns == 0) maxIns = minIns;

    // Every program is a fixed alternating sequence over the seeded variable x:
    //   PRINT("Value from: " + x), ADD(x, x, [1..10]), PRINT, ADD, ...
    // Each command is one atomic instruction, so the count maps 1:1 to [minIns, maxIns].
    const int count = randInt(static_cast<int>(minIns), static_cast<int>(maxIns));
    std::vector<std::unique_ptr<IInstruction>> program;
    program.reserve(count);
    for (int i = 0; i < count; ++i) {
        if (i % 2 == 0) {
            program.push_back(std::make_unique<PrintCommand>("Value from: ", "x"));
        } else {
            program.push_back(std::make_unique<AddCommand>(
                "x", Operand::var("x"),
                Operand::lit(static_cast<uint16_t>(randInt(1, 10)))));
        }
    }

    return std::make_unique<Process>(pid, name, std::move(program));
}

std::function<std::unique_ptr<IProcess>(const std::string&, int)>
makeProcessFactory(uint32_t minIns, uint32_t maxIns) {
    return [minIns, maxIns](const std::string& name, int pid) -> std::unique_ptr<IProcess> {
        return InstructionGenerator::generate(name, pid, minIns, maxIns);
    };
}
