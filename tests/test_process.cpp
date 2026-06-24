#include <gtest/gtest.h>

#include "process/Instructions.h"
#include "process/Process.h"

#include <memory>
#include <string>
#include <vector>

namespace {

using Program = std::vector<std::unique_ptr<IInstruction>>;

template <typename... Cmds>
std::unique_ptr<Process> makeProcess(Cmds&&... cmds) {
    Program p;
    (p.push_back(std::forward<Cmds>(cmds)), ...);
    return std::make_unique<Process>(1, "p01", std::move(p));
}

// Mimic the CPU + sleep manager: run a process to FINISHED, advancing sleeps.
// Returns the number of executeNext() calls (≈ instruction ticks, ignoring delays).
int runToCompletion(Process& proc, int maxTicks = 100000) {
    int execCalls = 0;
    for (int tick = 0; tick < maxTicks; ++tick) {
        if (proc.isFinished()) break;
        if (proc.getState() == ProcessState::WAITING) {
            proc.tickSleep();
            continue;
        }
        proc.executeNext(static_cast<uint64_t>(tick));
        ++execCalls;
    }
    return execCalls;
}

}  // namespace

TEST(ProcessTest, PrintDefaultMessage) {
    auto proc = makeProcess(std::make_unique<PrintCommand>());
    runToCompletion(*proc);
    ASSERT_EQ(proc->getPrintLog().size(), 1u);
    EXPECT_EQ(proc->getPrintLog()[0], "Hello world from p01!");
    EXPECT_TRUE(proc->isFinished());
}

TEST(ProcessTest, PrintConcatenatesVariable) {
    auto proc = makeProcess(
        std::make_unique<DeclareCommand>("x", 42),
        std::make_unique<PrintCommand>("x = ", "x"));
    runToCompletion(*proc);
    ASSERT_EQ(proc->getPrintLog().size(), 1u);
    EXPECT_EQ(proc->getPrintLog()[0], "x = 42");
}

TEST(ProcessTest, AddClampsAtMax) {
    auto proc = makeProcess(
        std::make_unique<DeclareCommand>("x", 60000),
        std::make_unique<AddCommand>("x", Operand::var("x"), Operand::lit(10000)),
        std::make_unique<PrintCommand>("", "x"));
    runToCompletion(*proc);
    EXPECT_EQ(proc->getPrintLog().back(), "65535");  // 70000 clamped, not wrapped
}

TEST(ProcessTest, SubtractFloorsAtZero) {
    auto proc = makeProcess(
        std::make_unique<DeclareCommand>("x", 5),
        std::make_unique<SubtractCommand>("x", Operand::var("x"), Operand::lit(10)),
        std::make_unique<PrintCommand>("", "x"));
    runToCompletion(*proc);
    EXPECT_EQ(proc->getPrintLog().back(), "0");  // -5 floored to 0, no underflow wrap
}

TEST(ProcessTest, UndeclaredVariableAutoDeclaresZero) {
    auto proc = makeProcess(
        std::make_unique<AddCommand>("sum", Operand::var("ghost"), Operand::lit(7)),
        std::make_unique<PrintCommand>("", "sum"));
    runToCompletion(*proc);
    EXPECT_EQ(proc->getPrintLog().back(), "7");
}

TEST(ProcessTest, SleepRelinquishesThenResumes) {
    auto proc = makeProcess(
        std::make_unique<PrintCommand>("before"),
        std::make_unique<SleepCommand>(3),
        std::make_unique<PrintCommand>("after"));

    // Tick 0: PRINT "before".
    proc->executeNext(0);
    EXPECT_EQ(proc->getState(), ProcessState::RUNNING);

    // Tick 1: SLEEP -> relinquishes the CPU.
    proc->executeNext(1);
    EXPECT_EQ(proc->getState(), ProcessState::WAITING);

    // 3 sleep ticks before it returns to READY.
    proc->tickSleep();
    EXPECT_EQ(proc->getState(), ProcessState::WAITING);
    proc->tickSleep();
    EXPECT_EQ(proc->getState(), ProcessState::WAITING);
    proc->tickSleep();
    EXPECT_EQ(proc->getState(), ProcessState::READY);

    runToCompletion(*proc);
    EXPECT_TRUE(proc->isFinished());
    ASSERT_EQ(proc->getPrintLog().size(), 2u);
    EXPECT_EQ(proc->getPrintLog()[1], "after");
}

TEST(ProcessTest, ForLoopRepeatsBody) {
    Program body;
    body.push_back(std::make_unique<PrintCommand>("tick"));
    auto proc = makeProcess(std::make_unique<ForCommand>(std::move(body), 4));

    // totalLines is the fully-expanded atomic count: 4 iterations * 1 instr.
    EXPECT_EQ(proc->getTotalLines(), 4);
    int execs = runToCompletion(*proc);
    EXPECT_EQ(execs, 4);
    EXPECT_EQ(proc->getPrintLog().size(), 4u);
    EXPECT_EQ(proc->getCurrentLine(), proc->getTotalLines());
}

TEST(ProcessTest, NestedForExpandsByProduct) {
    // FOR(3) { FOR(2) { PRINT } } -> 6 atomic executions.
    Program inner;
    inner.push_back(std::make_unique<PrintCommand>("hi"));
    Program outer;
    outer.push_back(std::make_unique<ForCommand>(std::move(inner), 2));
    auto proc = makeProcess(std::make_unique<ForCommand>(std::move(outer), 3));

    EXPECT_EQ(proc->getTotalLines(), 6);
    EXPECT_EQ(runToCompletion(*proc), 6);
}

TEST(ProcessTest, SleepInsideForRelinquishesEachIteration) {
    // FOR(2) { SLEEP(2); PRINT } — verifies SLEEP yields mid-loop, not all at once.
    Program body;
    body.push_back(std::make_unique<SleepCommand>(2));
    body.push_back(std::make_unique<PrintCommand>("done"));
    auto proc = makeProcess(std::make_unique<ForCommand>(std::move(body), 2));

    int sleepEpisodes = 0;
    bool wasSleeping = false;
    for (int tick = 0; tick < 1000 && !proc->isFinished(); ++tick) {
        if (proc->getState() == ProcessState::WAITING) {
            if (!wasSleeping) ++sleepEpisodes;
            wasSleeping = true;
            proc->tickSleep();
        } else {
            wasSleeping = false;
            proc->executeNext(tick);
        }
    }
    EXPECT_TRUE(proc->isFinished());
    EXPECT_EQ(sleepEpisodes, 2);              // one SLEEP per loop iteration
    EXPECT_EQ(proc->getPrintLog().size(), 2u);
}

TEST(ProcessTest, ZeroLengthProcessIsImmediatelyFinished) {
    auto proc = std::make_unique<Process>(7, "empty", Program{});
    EXPECT_TRUE(proc->isFinished());
    EXPECT_EQ(proc->getTotalLines(), 0);
}

TEST(GeneratorTest, ProducesRunnableProcessWithinRange) {
    for (int trial = 0; trial < 50; ++trial) {
        auto proc = InstructionGenerator::generate("p01", 1, 5, 15);
        EXPECT_EQ(proc->getPID(), 1);
        EXPECT_EQ(proc->getName(), "p01");
        EXPECT_GT(proc->getTotalLines(), 0);
        EXPECT_FALSE(proc->isFinished());

        runToCompletion(*proc);
        EXPECT_TRUE(proc->isFinished());
        EXPECT_EQ(proc->getCurrentLine(), proc->getTotalLines());
    }
}

TEST(GeneratorTest, FactoryAdapterYieldsIProcess) {
    auto factory = makeProcessFactory(3, 8);
    std::unique_ptr<IProcess> p = factory("p99", 99);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->getName(), "p99");
    EXPECT_EQ(p->getPID(), 99);
    EXPECT_GT(p->getTotalLines(), 0);
}
