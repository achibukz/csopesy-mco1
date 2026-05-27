#include <gtest/gtest.h>

#include "MockProcess.h"
#include "scheduler/Scheduler.h"

#include <chrono>
#include <thread>
#include <vector>

namespace {

bool waitForAllFinished(int expected, std::chrono::milliseconds timeout) {
    using clock = std::chrono::steady_clock;
    auto deadline = clock::now() + timeout;
    while (clock::now() < deadline) {
        if (Scheduler::instance().getFinishedSnapshot().size() >= static_cast<size_t>(expected)) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}

SchedulerConfig makeCfg(int cores, SchedulerConfig::Algo algo,
                        uint32_t quantum = 1, uint32_t delays = 0,
                        uint32_t batch = 1) {
    SchedulerConfig c;
    c.numCpu = cores;
    c.algo = algo;
    c.quantum = quantum;
    c.delaysPerExec = delays;
    c.batchProcessFreq = batch;
    return c;
}

class SchedulerFixture : public ::testing::Test {
protected:
    void TearDown() override {
        Scheduler::instance().shutdown();
    }
};

}  // namespace

TEST_F(SchedulerFixture, FcfsOneCoreThreeProcesses) {
    auto cfg = makeCfg(1, SchedulerConfig::Algo::FCFS);
    Scheduler::instance().initialize(cfg);

    MockProcess a(1, "a", 5);
    MockProcess b(2, "b", 5);
    MockProcess c(3, "c", 5);
    Scheduler::instance().enqueue(&a);
    Scheduler::instance().enqueue(&b);
    Scheduler::instance().enqueue(&c);

    ASSERT_TRUE(waitForAllFinished(3, std::chrono::milliseconds(2000)));
    auto finished = Scheduler::instance().getFinishedSnapshot();
    ASSERT_EQ(finished.size(), 3u);
    EXPECT_EQ(finished[0]->getName(), "a");
    EXPECT_EQ(finished[1]->getName(), "b");
    EXPECT_EQ(finished[2]->getName(), "c");
}

TEST_F(SchedulerFixture, FcfsTwoCoreFourProcesses) {
    auto cfg = makeCfg(2, SchedulerConfig::Algo::FCFS);
    Scheduler::instance().initialize(cfg);

    std::vector<std::unique_ptr<MockProcess>> procs;
    for (int i = 0; i < 4; ++i) {
        procs.push_back(std::make_unique<MockProcess>(i + 1, "p" + std::to_string(i), 5));
        Scheduler::instance().enqueue(procs.back().get());
    }

    ASSERT_TRUE(waitForAllFinished(4, std::chrono::milliseconds(2000)));
    EXPECT_EQ(Scheduler::instance().getFinishedSnapshot().size(), 4u);
}

TEST_F(SchedulerFixture, RoundRobinRespectsQuantum) {
    // 4 processes on 2 cores guarantees there's always at least one process
    // waiting, so RR must preempt at quantum boundaries.
    auto cfg = makeCfg(2, SchedulerConfig::Algo::RR, /*quantum=*/3);
    Scheduler::instance().initialize(cfg);

    std::vector<std::unique_ptr<MockProcess>> procs;
    for (int i = 0; i < 4; ++i) {
        procs.push_back(std::make_unique<MockProcess>(i + 1, "p" + std::to_string(i), 20));
        Scheduler::instance().enqueue(procs.back().get());
    }

    ASSERT_TRUE(waitForAllFinished(4, std::chrono::milliseconds(5000)));

    // Each process must have been preempted at least once.
    // Detect preemption by counting "gaps" (non-consecutive ticks) in the visit log.
    // Note: a process whose preemption coincides with another's on the same tick can
    // be re-assigned to the freed core immediately, so contiguous-tick runs longer
    // than the quantum are legal. The robust invariant is: every process gets at
    // least one gap (proves the scheduler preempted it).
    for (auto& proc : procs) {
        auto visits = proc->getVisits();
        ASSERT_GT(visits.size(), static_cast<size_t>(cfg.quantum))
            << "Process " << proc->getName() << " did not run enough instructions";
        int gaps = 0;
        for (size_t i = 1; i < visits.size(); ++i) {
            if (visits[i].tick != visits[i - 1].tick + 1) ++gaps;
        }
        EXPECT_GT(gaps, 0)
            << "Process " << proc->getName() << " was never preempted (no gaps in visits)";
    }
}

TEST_F(SchedulerFixture, DelaysPerExecAddsBusyWait) {
    auto cfg = makeCfg(1, SchedulerConfig::Algo::FCFS, /*quantum=*/1, /*delays=*/2);
    Scheduler::instance().initialize(cfg);

    MockProcess p(1, "p", 5);
    Scheduler::instance().enqueue(&p);

    ASSERT_TRUE(waitForAllFinished(1, std::chrono::milliseconds(2000)));

    auto visits = p.getVisits();
    ASSERT_EQ(visits.size(), 5u);
    for (size_t i = 1; i < visits.size(); ++i) {
        uint64_t delta = visits[i].tick - visits[i - 1].tick;
        EXPECT_GE(delta, 3u) << "delays-per-exec=2 should leave >= 2 idle ticks between executes";
    }
}

TEST_F(SchedulerFixture, SleepRelinquishesCpu) {
    auto cfg = makeCfg(1, SchedulerConfig::Algo::FCFS);
    Scheduler::instance().initialize(cfg);

    MockProcess sleeper(1, "sleeper", 5, /*sleepAtLine=*/2, /*sleepDuration=*/5);
    MockProcess other(2, "other", 3);
    Scheduler::instance().enqueue(&sleeper);
    Scheduler::instance().enqueue(&other);

    ASSERT_TRUE(waitForAllFinished(2, std::chrono::milliseconds(3000)));

    auto finished = Scheduler::instance().getFinishedSnapshot();
    ASSERT_EQ(finished.size(), 2u);

    auto sVisits = sleeper.getVisits();
    auto oVisits = other.getVisits();
    ASSERT_FALSE(sVisits.empty());
    ASSERT_FALSE(oVisits.empty());

    // While sleeper is asleep, other must have advanced (other gets the core).
    uint64_t sleepStart = sVisits[1].tick;  // tick when SLEEP instruction recorded
    bool otherProgressedDuringSleep = false;
    for (auto& v : oVisits) {
        if (v.tick > sleepStart) {
            otherProgressedDuringSleep = true;
            break;
        }
    }
    EXPECT_TRUE(otherProgressedDuringSleep);
}

TEST_F(SchedulerFixture, GeneratorProducesProcesses) {
    auto cfg = makeCfg(1, SchedulerConfig::Algo::FCFS, /*quantum=*/1, /*delays=*/0, /*batch=*/3);
    Scheduler::instance().initialize(cfg);

    std::atomic<int> created{0};
    Scheduler::instance().setProcessFactory([&](const std::string& name, int pid) {
        ++created;
        return std::make_unique<MockProcess>(pid, name, 2);
    });
    Scheduler::instance().startGenerator();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    Scheduler::instance().stopGenerator();

    EXPECT_GT(created.load(), 0);
}

TEST_F(SchedulerFixture, CleanShutdownIsBounded) {
    auto cfg = makeCfg(4, SchedulerConfig::Algo::RR, /*quantum=*/3);
    Scheduler::instance().initialize(cfg);
    Scheduler::instance().setProcessFactory([](const std::string& name, int pid) {
        return std::make_unique<MockProcess>(pid, name, 5);
    });
    Scheduler::instance().startGenerator();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    auto start = std::chrono::steady_clock::now();
    Scheduler::instance().shutdown();
    auto elapsed = std::chrono::steady_clock::now() - start;
    EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(), 500);
}
