#include "process/Instructions.h"

#include "process/Process.h"
#include "scheduler/IProcess.h"

#include <algorithm>
#include <array>
#include <random>
#include <string>

// ---------------------------------------------------------------------------
// IInstruction default body() — shared empty list for non-block instructions.
// ---------------------------------------------------------------------------
const std::vector<std::unique_ptr<IInstruction>>& IInstruction::body() const {
    static const std::vector<std::unique_ptr<IInstruction>> kEmpty;
    return kEmpty;
}

// ---------------------------------------------------------------------------
// Command implementations
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// Random instruction generation
// ---------------------------------------------------------------------------
namespace {

std::mt19937& rng() {
    // Per-thread generator: the scheduler's generator thread and the console
    // thread (screen -s) may both call into here concurrently.
    static thread_local std::mt19937 engine{std::random_device{}()};
    return engine;
}

int randInt(int lo, int hi) {
    std::uniform_int_distribution<int> d(lo, hi);
    return d(rng());
}

// Small fixed pool so ADD/SUBTRACT operands tend to reference real variables;
// missing ones auto-declare to 0 anyway.
const std::array<std::string, 5> kVarPool{"x", "y", "z", "a", "b"};

const std::string& randVar() {
    return kVarPool[randInt(0, static_cast<int>(kVarPool.size()) - 1)];
}

Operand randOperand() {
    if (randInt(0, 1) == 0) return Operand::lit(static_cast<uint16_t>(randInt(0, 500)));
    return Operand::var(randVar());
}

}  // namespace

std::unique_ptr<IInstruction> InstructionGenerator::randomCommand(int depthRemaining) {
    // Allow FOR only while another level of nesting is still permitted.
    const bool allowFor = depthRemaining > 1;
    const int kinds = allowFor ? 6 : 5;

    switch (randInt(0, kinds - 1)) {
        case 0:  // PRINT (sometimes concatenating a variable)
            if (randInt(0, 1) == 0)
                return std::make_unique<PrintCommand>();
            return std::make_unique<PrintCommand>("Value of " + randVar() + ": ", randVar());
        case 1:  // DECLARE
            return std::make_unique<DeclareCommand>(randVar(),
                                                    static_cast<uint16_t>(randInt(0, 1000)));
        case 2:  // ADD
            return std::make_unique<AddCommand>(randVar(), randOperand(), randOperand());
        case 3:  // SUBTRACT
            return std::make_unique<SubtractCommand>(randVar(), randOperand(), randOperand());
        case 4:  // SLEEP
            return std::make_unique<SleepCommand>(static_cast<uint8_t>(randInt(1, 8)));
        default: {  // FOR
            int bodySize = randInt(1, 3);
            std::vector<std::unique_ptr<IInstruction>> body;
            body.reserve(bodySize);
            for (int i = 0; i < bodySize; ++i)
                body.push_back(randomCommand(depthRemaining - 1));
            return std::make_unique<ForCommand>(std::move(body), randInt(2, 5));
        }
    }
}

std::unique_ptr<Process> InstructionGenerator::generate(const std::string& name, int pid,
                                                        uint32_t minIns, uint32_t maxIns) {
    if (maxIns < minIns) std::swap(minIns, maxIns);
    if (minIns == 0) minIns = 1;  // never generate a zero-length program
    if (maxIns == 0) maxIns = minIns;

    const int count = randInt(static_cast<int>(minIns), static_cast<int>(maxIns));
    std::vector<std::unique_ptr<IInstruction>> program;
    program.reserve(count);
    for (int i = 0; i < count; ++i)
        program.push_back(randomCommand(kMaxForDepth));

    return std::make_unique<Process>(pid, name, std::move(program));
}

// ---------------------------------------------------------------------------
// Factory adapter for the scheduler
// ---------------------------------------------------------------------------
std::function<std::unique_ptr<IProcess>(const std::string&, int)>
makeProcessFactory(uint32_t minIns, uint32_t maxIns) {
    return [minIns, maxIns](const std::string& name, int pid) -> std::unique_ptr<IProcess> {
        return InstructionGenerator::generate(name, pid, minIns, maxIns);
    };
}
