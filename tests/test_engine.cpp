#include <gtest/gtest.h>

#include "MockProcess.h"
#include "scheduler/SchedulerEngine.h"
#include "scheduler/SchedulingPolicy.h"

#include <atomic>
#include <memory>

namespace {

SchedulerConfig makeConfig(uint32_t batchFreq = 1) {
    SchedulerConfig c;
    c.numCpu = 1;
    c.algo = SchedulerConfig::Algo::FCFS;
    c.quantum = 1;
    c.delaysPerExec = 0;
    c.batchProcessFreq = batchFreq;
    return c;
}

}  // namespace

TEST(EngineTest, EnqueuePopOrderFcfs) {
    FCFSPolicy policy;
    SchedulerEngine engine(makeConfig(), policy);

    MockProcess a(1, "a", 5);
    MockProcess b(2, "b", 5);
    MockProcess c(3, "c", 5);
    engine.enqueueReady(&a);
    engine.enqueueReady(&b);
    engine.enqueueReady(&c);

    EXPECT_EQ(engine.popReady(), &a);
    EXPECT_EQ(engine.popReady(), &b);
    EXPECT_EQ(engine.popReady(), &c);
    EXPECT_EQ(engine.popReady(), nullptr);
}

TEST(EngineTest, StepOnceIncrementsTick) {
    FCFSPolicy policy;
    SchedulerEngine engine(makeConfig(), policy);
    EXPECT_EQ(engine.currentTick(), 0u);
    engine.stepOnce();
    EXPECT_EQ(engine.currentTick(), 1u);
    engine.stepOnce();
    engine.stepOnce();
    EXPECT_EQ(engine.currentTick(), 3u);
}

TEST(EngineTest, GeneratorFiresEveryBatchProcessFreqTicks) {
    FCFSPolicy policy;
    SchedulerEngine engine(makeConfig(/*batchFreq=*/3), policy);

    int created = 0;
    engine.setProcessFactory([&](const std::string& name, int pid) {
        ++created;
        return std::make_unique<MockProcess>(pid, name, 1);
    });
    engine.startGenerator();

    for (int i = 0; i < 9; ++i) engine.stepOnce();
    EXPECT_EQ(created, 3);
}

TEST(EngineTest, GeneratorDisabledByDefault) {
    FCFSPolicy policy;
    SchedulerEngine engine(makeConfig(1), policy);

    int created = 0;
    engine.setProcessFactory([&](const std::string& name, int pid) {
        ++created;
        return std::make_unique<MockProcess>(pid, name, 1);
    });
    for (int i = 0; i < 5; ++i) engine.stepOnce();
    EXPECT_EQ(created, 0);
}

TEST(EngineTest, RunningAndFinishedSnapshotsAreCopies) {
    FCFSPolicy policy;
    SchedulerEngine engine(makeConfig(), policy);
    MockProcess a(1, "a", 5);
    engine.markRunning(&a, 0);

    auto running = engine.snapshotRunning();
    EXPECT_EQ(running.size(), 1u);
    EXPECT_EQ(running[0], &a);

    engine.markFinished(&a);
    EXPECT_EQ(engine.snapshotRunning().size(), 0u);
    EXPECT_EQ(engine.snapshotFinished().size(), 1u);
    EXPECT_EQ(running.size(), 1u);
}

TEST(EngineTest, CoresUsedTracksRunning) {
    FCFSPolicy policy;
    SchedulerEngine engine(makeConfig(), policy);
    MockProcess a(1, "a", 5);
    MockProcess b(2, "b", 5);

    EXPECT_EQ(engine.coresUsed(), 0);
    engine.markRunning(&a, 0);
    EXPECT_EQ(engine.coresUsed(), 1);
    engine.markRunning(&b, 0);
    EXPECT_EQ(engine.coresUsed(), 2);
    engine.clearRunning(&a);
    EXPECT_EQ(engine.coresUsed(), 1);
    engine.markFinished(&b);
    EXPECT_EQ(engine.coresUsed(), 0);
}

TEST(EngineTest, PreemptRemovesFromRunningAndCallsPolicy) {
    RRPolicy policy;
    SchedulerEngine engine(makeConfig(), policy);
    MockProcess a(1, "a", 5);
    engine.markRunning(&a, 0);

    engine.preempt(&a, policy);
    EXPECT_EQ(engine.coresUsed(), 0);
    EXPECT_EQ(engine.snapshotRunning().size(), 0u);
    EXPECT_EQ(engine.popReady(), &a);
}

// DS line 417: "Never hold stateMutex_ across a process method call."
// Regression test for the tickSleepingProcesses deadlock risk.
TEST(EngineTest, TickSleepingDoesNotHoldStateMutex) {
    FCFSPolicy policy;
    SchedulerEngine engine(makeConfig(), policy);

    // Build a waiting process: sleeps at line 1 for 3 ticks. Drive it into
    // WAITING state before handing it to the engine.
    auto sleeper = std::make_unique<MockProcess>(
        1, "p01", /*totalInstructions=*/5,
        /*sleepAtLine=*/1, /*sleepDuration=*/3);
    sleeper->executeNext(0);
    ASSERT_EQ(sleeper->getState(), ProcessState::WAITING);

    MockProcess* raw = sleeper.get();
    engine.adoptSleeping(std::move(sleeper));

    // While engine ticks the sleeper, attempt to call a snapshot from inside
    // tickSleep. If stateMutex_ is still held, this would deadlock.
    std::atomic<bool> reentered{false};
    raw->tickSleepHook = [&]() {
        (void)engine.snapshotRunning();
        reentered.store(true);
    };

    engine.stepOnce();
    EXPECT_TRUE(reentered.load())
        << "tickSleep was not invoked, or engine still holds stateMutex_";
}

// DS line 422: "Never hold stateMutex_ across a process method call."
// The rebuild phase of tickSleepingProcesses calls p->getState() on every
// sleeper to decide whether to wake it. If that call happens under
// stateMutex_, a process whose getState() reenters the engine deadlocks.
TEST(EngineTest, TickSleepingDoesNotHoldStateMutexDuringRebuild) {
    FCFSPolicy policy;
    SchedulerEngine engine(makeConfig(), policy);

    auto sleeper = std::make_unique<MockProcess>(
        1, "p01", /*totalInstructions=*/5,
        /*sleepAtLine=*/1, /*sleepDuration=*/3);
    sleeper->executeNext(0);
    ASSERT_EQ(sleeper->getState(), ProcessState::WAITING);

    MockProcess* raw = sleeper.get();
    engine.adoptSleeping(std::move(sleeper));

    // Hook getState() so the engine's rebuild-phase call reenters a method
    // that needs stateMutex_. If the engine still holds the lock, this
    // deadlocks (CTest timeout will catch it).
    std::atomic<bool> reentered{false};
    raw->getStateHook = [&]() {
        (void)engine.snapshotRunning();
        reentered.store(true);
    };

    engine.stepOnce();
    EXPECT_TRUE(reentered.load())
        << "getState was not invoked, or engine still holds stateMutex_ "
           "during the rebuild phase";
}

// CR:139-141 — multiple sleepers with the same wake tick must all transition
// from WAITING to READY in the same engine step without dropping any.
TEST(EngineTest, SimultaneousAwakeningsAllReachReady) {
    FCFSPolicy policy;
    SchedulerEngine engine(makeConfig(), policy);

    for (int i = 1; i <= 5; ++i) {
        auto p = std::make_unique<MockProcess>(
            i, "p0" + std::to_string(i), /*totalInstructions=*/2,
            /*sleepAtLine=*/1, /*sleepDuration=*/1);
        p->executeNext(0);  // drive to WAITING
        ASSERT_EQ(p->getState(), ProcessState::WAITING);
        engine.adoptSleeping(std::move(p));
    }

    engine.stepOnce();  // single tick should wake all 5

    int popped = 0;
    while (engine.popReady() != nullptr) ++popped;
    EXPECT_EQ(popped, 5);
}

TEST(EngineTest, GeneratorNamesAreZeroPadded) {
    FCFSPolicy policy;
    SchedulerEngine engine(makeConfig(1), policy);

    std::vector<std::string> names;
    engine.setProcessFactory([&](const std::string& name, int pid) {
        names.push_back(name);
        return std::make_unique<MockProcess>(pid, name, 1);
    });
    engine.startGenerator();
    for (int i = 0; i < 3; ++i) engine.stepOnce();

    ASSERT_EQ(names.size(), 3u);
    EXPECT_EQ(names[0], "p01");
    EXPECT_EQ(names[1], "p02");
    EXPECT_EQ(names[2], "p03");
}
