#include <gtest/gtest.h>

#include "scheduler/IProcess.h"
#include "scheduler/SchedulingPolicy.h"

#include <chrono>
#include <queue>

namespace {

class StubProcess : public IProcess {
public:
    StubProcess(int pid, std::string name, int total)
        : pid_(pid), name_(std::move(name)), total_(total) {}

    int           getPID() const override         { return pid_; }
    std::string   getName() const override        { return name_; }
    ProcessState  getState() const override       { return state_; }
    int           getCurrentLine() const override { return current_; }
    int           getTotalLines() const override  { return total_; }
    bool          isFinished() const override     { return state_ == ProcessState::FINISHED; }

    void executeNext(uint64_t) override {
        if (current_ < total_) ++current_;
        if (current_ >= total_) state_ = ProcessState::FINISHED;
    }
    void tickSleep() override {}

    std::vector<std::string>              getPrintLog() const override { return {}; }
    std::chrono::system_clock::time_point getCreatedAt() const override { return {}; }

    void setState(ProcessState s) { state_ = s; }

private:
    int pid_;
    std::string name_;
    int total_;
    int current_ = 0;
    ProcessState state_ = ProcessState::READY;
};

}  // namespace

TEST(FCFSPolicyTest, PickNextPopsFront) {
    FCFSPolicy policy;
    std::queue<IProcess*> q;
    StubProcess a(1, "a", 10), b(2, "b", 10);
    q.push(&a);
    q.push(&b);

    EXPECT_EQ(policy.pickNext(q), &a);
    EXPECT_EQ(policy.pickNext(q), &b);
    EXPECT_EQ(policy.pickNext(q), nullptr);
}

TEST(FCFSPolicyTest, KeepsRunningUntilFinished) {
    FCFSPolicy policy;
    StubProcess p(1, "p", 5);
    EXPECT_TRUE(policy.shouldKeepRunning(&p, 1, 999));
    EXPECT_TRUE(policy.shouldKeepRunning(&p, 1000, 999));
    p.setState(ProcessState::FINISHED);
    EXPECT_FALSE(policy.shouldKeepRunning(&p, 1, 999));
}

TEST(FCFSPolicyTest, YieldsOnWaiting) {
    FCFSPolicy policy;
    StubProcess p(1, "p", 5);
    p.setState(ProcessState::WAITING);
    EXPECT_FALSE(policy.shouldKeepRunning(&p, 1, 999));
}

TEST(FCFSPolicyTest, OnPreemptIsNoOp) {
    FCFSPolicy policy;
    std::queue<IProcess*> q;
    StubProcess p(1, "p", 5);
    policy.onPreempt(&p, q);
    EXPECT_TRUE(q.empty());
}

TEST(RRPolicyTest, PickNextPopsFront) {
    RRPolicy policy;
    std::queue<IProcess*> q;
    StubProcess a(1, "a", 10), b(2, "b", 10);
    q.push(&a);
    q.push(&b);
    EXPECT_EQ(policy.pickNext(q), &a);
    EXPECT_EQ(policy.pickNext(q), &b);
}

TEST(RRPolicyTest, ShouldKeepRunningUntilQuantum) {
    RRPolicy policy;
    StubProcess p(1, "p", 50);
    EXPECT_TRUE(policy.shouldKeepRunning(&p, 0, 5));
    EXPECT_TRUE(policy.shouldKeepRunning(&p, 4, 5));
    EXPECT_FALSE(policy.shouldKeepRunning(&p, 5, 5));
    EXPECT_FALSE(policy.shouldKeepRunning(&p, 6, 5));
}

TEST(RRPolicyTest, OnPreemptPushesToBack) {
    RRPolicy policy;
    std::queue<IProcess*> q;
    StubProcess a(1, "a", 10), b(2, "b", 10);
    q.push(&a);
    policy.onPreempt(&b, q);
    EXPECT_EQ(q.front(), &a);
    q.pop();
    EXPECT_EQ(q.front(), &b);
}

TEST(RRPolicyTest, OnPreemptSkipsFinishedProcess) {
    RRPolicy policy;
    std::queue<IProcess*> q;
    StubProcess p(1, "p", 5);
    p.setState(ProcessState::FINISHED);
    policy.onPreempt(&p, q);
    EXPECT_TRUE(q.empty());
}

TEST(RRPolicyTest, EmptyQueueReturnsNull) {
    RRPolicy policy;
    std::queue<IProcess*> q;
    EXPECT_EQ(policy.pickNext(q), nullptr);
}

// CR:134-142 — RR with quantum=1 must preempt every tick.
TEST(PolicyTest, RoundRobinQuantumOnePreemptsEveryTick) {
    RRPolicy policy;
    StubProcess p(1, "p", 50);
    EXPECT_FALSE(policy.shouldKeepRunning(&p, /*ticksOnCore=*/1, /*quantum=*/1));
}

// CR:134-142 — quantum larger than the process length must let the process
// finish in one slice (degrades to FCFS).
TEST(PolicyTest, RoundRobinQuantumExceedsProcessLengthBehavesLikeFCFS) {
    RRPolicy policy;
    StubProcess p(1, "p", 5);
    for (int t = 1; t <= 5; ++t) {
        EXPECT_TRUE(policy.shouldKeepRunning(&p, t, /*quantum=*/1000))
            << "RR preempted before quantum at t=" << t;
    }
}
