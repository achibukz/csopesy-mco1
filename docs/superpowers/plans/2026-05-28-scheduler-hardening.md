# [FINISHED] Scheduler Hardening (Code Review + Edge Cases) Implementation Plan

> **Status:** FINISHED — 2026-05-28. All 11 tasks implemented. 32-test suite passing.
> Locking discipline fix (Task 2) and CPU delay type fix (Task 4) shipped; characterization
> tests for quantum boundaries, simultaneous wake, 128-core boot, CRLF config, snapshot
> concurrency, and empty REPL input all landed. Verified in observations 905-907.


> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Address the 1 critical finding from the Gemini code review and the spec-bounded edge cases from `docs/task1_3_edge_cases_and_tests.md`, with strict adherence to the existing scheduler specs.

**Architecture:** Two real defects to fix (lock-held-across-virtual-call in `tickSleepingProcesses`; `int` overflow in `CPU::delayTicksRemaining_`). The remaining work is characterization tests that lock in spec-compliant behavior at boundary inputs (quantum=1, quantum > process length, simultaneous wake, 128 cores, CRLF config, snapshot concurrency, empty REPL input).

**Tech Stack:** C++20, GoogleTest, CMake, `std::thread`/`std::mutex` (matches existing code).

**Authoritative specs (cited per task):**
- `docs/2026-05-26-CSOPESY-mco1-group-plan.md` (GP)
- `docs/2026-05-26-CSOPESY-mco1-code-reference.md` (CR)
- `docs/superpowers/specs/2026-05-26-track1-scheduler-design.md` (DS)
- `CLAUDE.md` (CM)

**Out-of-scope (explicitly):**
- Zero-instruction processes (CM/GP min-ins range `[1,∞)` — out of spec).
- 10,000-process starvation stress (no spec requirement; not a correctness concern).
- Track 2 / Track 4 features (Process, Instructions, ScreenManager, Reporter — separate tracks).

---

### Task 1: Add strict-spec rule to CLAUDE.md

**Files:**
- Modify: `CLAUDE.md` (append a new section)

- [ ] **Step 1: Append the spec-adherence section to CLAUDE.md**

Append at the end of `CLAUDE.md`:

```markdown

## Spec adherence (strict)
Strictly follow the specs. Do not hallucinate behavior, ranges, types, or features.

Authoritative specs for this project:
- `docs/2026-05-26-CSOPESY-mco1-group-plan.md`
- `docs/2026-05-26-CSOPESY-mco1-code-reference.md`
- `docs/superpowers/specs/2026-05-26-track1-scheduler-design.md`
- This `CLAUDE.md`

Rules:
- Do not add features, validation ranges, command names, instruction types, or
  config keys that are not in those docs.
- Do not invent test cases for scenarios outside spec bounds (e.g., do not
  test `min-ins = 0` when the spec range is `[1, ∞)`).
- If a spec is silent on a detail, ask before assuming — do not extrapolate
  from "what other OSes do."
- When citing a spec in code review or commit messages, include doc + line/section.
- Code review findings must reference the exact spec rule being violated.
```

- [ ] **Step 2: Verify the file edit applied**

Run: `tail -25 CLAUDE.md`
Expected: shows the new "Spec adherence (strict)" section.

- [ ] **Step 3: Commit**

```bash
git add CLAUDE.md
git commit -m "docs: add strict spec-adherence rule to CLAUDE.md"
```

---

### Task 2: Fix lock-held-across-virtual-call in `tickSleepingProcesses`

**Spec cited:** DS line 417 — *"Never hold `stateMutex_` across a process method call. Pop → release lock → execute → re-acquire as needed."*

**Defect:** `src/scheduler/SchedulerEngine.cpp:194` calls `p->tickSleep()` while holding `stateMutex_`. Track 2's eventual `Process::tickSleep` may touch scheduler state, deadlocking.

**Files:**
- Modify: `src/scheduler/SchedulerEngine.cpp` (function `tickSleepingProcesses`, currently lines 187–204)
- Modify: `tests/MockProcess.h` (add a hook to detect re-entry from `tickSleep`)
- Modify: `tests/MockProcess.cpp` (implement the hook)
- Modify: `tests/test_engine.cpp` (add the new test)

- [ ] **Step 1: Add a re-entry probe to MockProcess**

In `tests/MockProcess.h`, inside the `MockProcess` class declaration, add a public member:

```cpp
    // Called inside tickSleep() so tests can detect whether the engine
    // released stateMutex_ before invoking process methods. Default: no-op.
    std::function<void()> tickSleepHook;
```

Add `#include <functional>` to the top of `MockProcess.h` if not already present.

In `tests/MockProcess.cpp`, inside `void MockProcess::tickSleep()`, invoke the hook at the very top of the function body (before any state mutation):

```cpp
void MockProcess::tickSleep() {
    if (tickSleepHook) tickSleepHook();
    // ... existing body unchanged ...
}
```

(Keep the existing body verbatim; only insert the two-line hook call.)

- [ ] **Step 2: Write the failing test**

Append to `tests/test_engine.cpp`:

```cpp
// DS line 417: "Never hold stateMutex_ across a process method call."
// Regression test for the tickSleepingProcesses deadlock risk.
TEST(EngineTest, TickSleepingDoesNotHoldStateMutex) {
    SchedulerConfig cfg;
    cfg.numCpu = 1;
    cfg.algo = SchedulerConfig::Algo::FCFS;
    cfg.quantum = 1;
    cfg.delaysPerExec = 0;

    SchedulerEngine engine(cfg);

    auto sleeper = std::make_unique<MockProcess>(1, "p01");
    sleeper->setSleepTicks(3);  // start in SLEEPING state
    MockProcess* raw = sleeper.get();
    engine.adoptSleeping(std::move(sleeper));

    // While engine ticks the sleeper, attempt to call a snapshot from inside
    // tickSleep. If stateMutex_ is still held, this would deadlock.
    std::atomic<bool> reentered{false};
    raw->tickSleepHook = [&]() {
        // Must be able to acquire stateMutex_ from this thread.
        (void)engine.snapshotRunning();
        reentered.store(true);
    };

    engine.stepOnce();
    EXPECT_TRUE(reentered.load())
        << "tickSleep was not invoked, or engine still holds stateMutex_";
}
```

If `SchedulerEngine` does not yet expose an `adoptSleeping(std::unique_ptr<IProcess>)` helper, add a minimal test-only accessor — see Step 3.

- [ ] **Step 3: Add the test seam to SchedulerEngine (if missing)**

In `include/scheduler/SchedulerEngine.h`, add under the public section:

```cpp
    // Test seam: directly inject a process already in SLEEPING state.
    // Used only by engine tests to exercise tickSleepingProcesses.
    void adoptSleeping(std::unique_ptr<IProcess> p);
```

In `src/scheduler/SchedulerEngine.cpp`, implement:

```cpp
void SchedulerEngine::adoptSleeping(std::unique_ptr<IProcess> p) {
    std::lock_guard<std::mutex> lk(stateMutex_);
    IProcess* raw = p.get();
    owned_.push_back(std::move(p));
    sleepingProcs_.push_back(raw);
}
```

(If `owned_` is not the field name, replace with the existing owning-vector identifier — check `include/scheduler/SchedulerEngine.h` for the actual name.)

- [ ] **Step 4: Run the test to verify it fails (deadlock or timeout)**

Build and run with a 10s timeout:

```bash
cmake --build build -j && timeout 10 ./build/mco1_tests --gtest_filter=EngineTest.TickSleepingDoesNotHoldStateMutex
```

Expected: the test either deadlocks (exit code 124 from `timeout`) or the call inside the hook hangs. This confirms the bug.

- [ ] **Step 5: Apply the fix in `tickSleepingProcesses`**

Replace the body of `SchedulerEngine::tickSleepingProcesses()` in `src/scheduler/SchedulerEngine.cpp` with:

```cpp
void SchedulerEngine::tickSleepingProcesses() {
    // DS line 417: never hold stateMutex_ across a process method call.
    // 1) snapshot sleepers under lock
    std::vector<IProcess*> sleepers;
    {
        std::lock_guard<std::mutex> lk(stateMutex_);
        sleepers = sleepingProcs_;
    }

    // 2) tick each one with no lock held
    for (IProcess* p : sleepers) {
        p->tickSleep();
    }

    // 3) under lock, rebuild sleepingProcs_ and push woken ones to ready_
    std::lock_guard<std::mutex> lk(stateMutex_);
    std::vector<IProcess*> stillSleeping;
    stillSleeping.reserve(sleepingProcs_.size());
    for (IProcess* p : sleepingProcs_) {
        if (p->getState() == ProcessState::SLEEPING) {
            stillSleeping.push_back(p);
        } else {
            ready_.push(p);
        }
    }
    sleepingProcs_.swap(stillSleeping);
}
```

Note: rebuild from the engine's *current* `sleepingProcs_` (not the snapshot), so any sleepers added between steps 1 and 3 are preserved.

- [ ] **Step 6: Run all tests; expect 100% pass**

```bash
cmake --build build -j && ctest --test-dir build --output-on-failure
```

Expected: previously-failing test now passes; all other tests still pass.

- [ ] **Step 7: Commit**

```bash
git add include/scheduler/SchedulerEngine.h src/scheduler/SchedulerEngine.cpp \
        tests/MockProcess.h tests/MockProcess.cpp tests/test_engine.cpp
git commit -m "fix(scheduler): release stateMutex_ before calling tickSleep (DS:417)"
```

---

### Task 3: Fix `int` overflow on `delays-per-exec` near `uint32_t` max

**Spec cited:** GP line 193 — *"`delays-per-exec` — busy-wait between instructions [0, 2³²]"*. CM table — *"`delays-per-exec` int [0,∞)"*.

**Defect:** `src/scheduler/CPU.cpp:51` does `delayTicksRemaining_ = static_cast<int>(delaysPerExec_)`. `delayTicksRemaining_` is declared `int` in `include/scheduler/CPU.h:41`. For `delaysPerExec_` > `INT_MAX` (~2.147 billion), the cast produces a negative value, the `> 0` guard fails immediately, and the busy-wait is skipped entirely.

**Files:**
- Modify: `include/scheduler/CPU.h` (change member type)
- Modify: `src/scheduler/CPU.cpp` (remove the lossy cast)
- Modify: `tests/test_scheduler.cpp` (or `tests/test_engine.cpp`) — add regression test

- [ ] **Step 1: Write the failing test**

Append to `tests/test_engine.cpp`:

```cpp
// GP line 193: delays-per-exec range is [0, 2^32). Values above INT_MAX must
// still busy-wait correctly. Regression test for CPU::delayTicksRemaining_ type.
TEST(EngineTest, LargeDelaysPerExecDoesNotUnderflow) {
    SchedulerConfig cfg;
    cfg.numCpu = 1;
    cfg.algo = SchedulerConfig::Algo::FCFS;
    cfg.quantum = 1;
    cfg.delaysPerExec = 3'000'000'000u;  // > INT_MAX, within uint32 range

    SchedulerEngine engine(cfg);

    auto proc = std::make_unique<MockProcess>(1, "p01");
    proc->setTotalInstructions(2);
    MockProcess* raw = proc.get();
    engine.adoptReady(std::move(proc));

    // Drive a few ticks; the CPU should still be busy-waiting, not executing
    // instructions back-to-back.
    for (int i = 0; i < 10; ++i) engine.stepOnce();

    EXPECT_LE(raw->executedCount(), 1u)
        << "CPU treated large delays-per-exec as zero — int overflow.";
}
```

Add `adoptReady` and `setTotalInstructions`/`executedCount` if not already present (mirror existing MockProcess + engine test seams).

- [ ] **Step 2: Run the test to verify it fails**

```bash
cmake --build build -j && ./build/mco1_tests --gtest_filter=EngineTest.LargeDelaysPerExecDoesNotUnderflow
```

Expected: FAIL with `executedCount() > 1` (delay was skipped entirely).

- [ ] **Step 3: Apply the fix — widen the counter type**

In `include/scheduler/CPU.h`, change line 41 from:

```cpp
    int                    delayTicksRemaining_ = 0;
```

to:

```cpp
    uint64_t               delayTicksRemaining_ = 0;
```

In `src/scheduler/CPU.cpp` line 51, change:

```cpp
        delayTicksRemaining_ = static_cast<int>(delaysPerExec_);
```

to:

```cpp
        delayTicksRemaining_ = delaysPerExec_;  // uint32 → uint64, no narrowing
```

- [ ] **Step 4: Run all tests**

```bash
cmake --build build -j && ctest --test-dir build --output-on-failure
```

Expected: new test passes; all others still pass.

- [ ] **Step 5: Commit**

```bash
git add include/scheduler/CPU.h src/scheduler/CPU.cpp tests/test_engine.cpp
git commit -m "fix(cpu): widen delayTicksRemaining_ to uint64_t (GP:193)"
```

---

### Task 4: Lock in RR quantum-boundary behavior

**Spec cited:** CR lines 134–142 (`runRR_oneSlice`) — order is `quantumRemaining > 0 AND not_finished AND not_sleeping`, then check `finished` BEFORE re-enqueue.

**Files:**
- Modify: `tests/test_policy.cpp` (pure-logic tests, no threads)

- [ ] **Step 1: Add quantum-thrashing and quantum-overshoot tests**

Append to `tests/test_policy.cpp`:

```cpp
// CR:134-142 — RR with quantum=1 must preempt every tick.
TEST(PolicyTest, RoundRobinQuantumOnePreemptsEveryTick) {
    RRPolicy policy(/*quantum=*/1);
    EXPECT_FALSE(policy.shouldKeepRunning(/*ticksOnCpu=*/1));
}

// CR:134-142 — quantum larger than the process length must let the process
// finish in one slice (degrades to FCFS).
TEST(PolicyTest, RoundRobinQuantumExceedsProcessLengthBehavesLikeFCFS) {
    RRPolicy policy(/*quantum=*/1000);
    // 5 ticks of execution should not trigger preemption.
    for (uint32_t t = 1; t <= 5; ++t) {
        EXPECT_TRUE(policy.shouldKeepRunning(t))
            << "RR preempted before quantum at t=" << t;
    }
}
```

If `shouldKeepRunning`'s actual signature differs, match it from `include/scheduler/SchedulingPolicy.h`.

- [ ] **Step 2: Run the tests**

```bash
cmake --build build -j && ./build/mco1_tests --gtest_filter=PolicyTest.RoundRobin*
```

Expected: PASS for both.

- [ ] **Step 3: Commit**

```bash
git add tests/test_policy.cpp
git commit -m "test: quantum-1 thrashing and quantum>length cases (CR:134)"
```

---

### Task 5: Process finishing on the same tick its quantum expires

**Spec cited:** CR lines 139–141 — *"if process.finished(): move to finishedProcs"* runs before any re-enqueue check.

**Files:**
- Modify: `tests/test_engine.cpp`

- [ ] **Step 1: Add the test**

Append to `tests/test_engine.cpp`:

```cpp
// CR:139-141 — when a process completes on the same tick its quantum would
// expire, it must end up in FINISHED, not back on READY.
TEST(EngineTest, RRFinishOnQuantumBoundaryGoesToFinishedNotReady) {
    SchedulerConfig cfg;
    cfg.numCpu = 1;
    cfg.algo = SchedulerConfig::Algo::RR;
    cfg.quantum = 3;
    cfg.delaysPerExec = 0;

    SchedulerEngine engine(cfg);

    auto proc = std::make_unique<MockProcess>(1, "p01");
    proc->setTotalInstructions(3);  // finishes exactly at quantum boundary
    engine.adoptReady(std::move(proc));

    for (int i = 0; i < 5; ++i) engine.stepOnce();

    auto finished = engine.snapshotFinished();
    auto ready    = engine.snapshotReady();
    EXPECT_EQ(finished.size(), 1u);
    EXPECT_TRUE(ready.empty()) << "process re-enqueued after final instruction";
}
```

If `snapshotReady()` does not exist, use whatever ready-queue introspection the existing tests use (see other engine tests for the exact name).

- [ ] **Step 2: Run and commit**

```bash
cmake --build build -j && ./build/mco1_tests --gtest_filter=EngineTest.RRFinishOnQuantumBoundary*
git add tests/test_engine.cpp
git commit -m "test: RR completion on quantum boundary lands in FINISHED (CR:139)"
```

---

### Task 6: Simultaneous awakenings

**Spec cited:** DS line 417 (lock discipline) + Task 2 fix above — all sleepers ticked once per global tick.

**Files:**
- Modify: `tests/test_engine.cpp`

- [ ] **Step 1: Add the test**

Append to `tests/test_engine.cpp`:

```cpp
// Multiple sleepers with the same wake tick must all transition to READY in
// the same engine step without dropping any.
TEST(EngineTest, SimultaneousAwakeningsAllReachReady) {
    SchedulerConfig cfg;
    cfg.numCpu = 1;
    cfg.algo = SchedulerConfig::Algo::FCFS;
    cfg.quantum = 1;
    cfg.delaysPerExec = 0;
    SchedulerEngine engine(cfg);

    for (int i = 1; i <= 5; ++i) {
        auto p = std::make_unique<MockProcess>(i, "p0" + std::to_string(i));
        p->setSleepTicks(1);
        engine.adoptSleeping(std::move(p));
    }

    engine.stepOnce();  // single tick should wake all 5

    EXPECT_EQ(engine.snapshotReady().size(), 5u);
}
```

- [ ] **Step 2: Run and commit**

```bash
cmake --build build -j && ./build/mco1_tests --gtest_filter=EngineTest.SimultaneousAwakenings*
git add tests/test_engine.cpp
git commit -m "test: simultaneous sleeper wake-ups (DS:417)"
```

---

### Task 7: num-cpu = 128 smoke test

**Spec cited:** CM table — *"num-cpu int [1,128]"*. Upper-bound input must run without crashing.

**Files:**
- Modify: `tests/test_scheduler.cpp`

- [ ] **Step 1: Add the test**

Append to `tests/test_scheduler.cpp`:

```cpp
// CM: num-cpu upper bound. Booting 128 worker threads must not crash.
TEST(SchedulerFixture, BootsWithMaxNumCpu) {
    SchedulerConfig cfg;
    cfg.numCpu = 128;
    cfg.algo = SchedulerConfig::Algo::FCFS;
    cfg.quantum = 1;
    cfg.delaysPerExec = 0;

    Scheduler::instance().initialize(cfg, /*factory=*/nullptr);
    // No processes generated (factory is null). Just make sure start/stop
    // round-trip cleanly.
    Scheduler::instance().startGenerator();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    Scheduler::instance().stopGenerator();
    Scheduler::instance().shutdown();
    SUCCEED();
}
```

If `Scheduler::instance().initialize` has a different factory parameter shape, match the existing fixture in the same file.

- [ ] **Step 2: Run and commit**

```bash
cmake --build build -j && ./build/mco1_tests --gtest_filter=SchedulerFixture.BootsWithMaxNumCpu
git add tests/test_scheduler.cpp
git commit -m "test: scheduler boots with num-cpu=128 (CM upper bound)"
```

---

### Task 8: Config robustness to CRLF and aggressive whitespace

**Spec cited:** CM config table; GP §3 (config parser). Spec does not require any specific line-ending behavior, but `parseConfigFromString` already attempts to handle `\r` (see `src/config/Config.cpp:94`). These tests pin the current intent.

**Files:**
- Modify: `tests/test_config.cpp`

- [ ] **Step 1: Add the tests**

Append to `tests/test_config.cpp`:

```cpp
// Parser must accept CRLF line endings.
TEST(ConfigTest, ParsesCrlfLineEndings) {
    const std::string body =
        "num-cpu 4\r\n"
        "scheduler rr\r\n"
        "quantum-cycles 5\r\n"
        "batch-process-freq 1\r\n"
        "min-ins 1\r\n"
        "max-ins 10\r\n"
        "delays-per-exec 0\r\n";
    EXPECT_NO_THROW({
        Config c = parseConfigFromString(body);
        EXPECT_EQ(c.numCpu, 4u);
        EXPECT_EQ(c.scheduler, "rr");
    });
}

// Parser must tolerate trailing spaces and multiple blank lines.
TEST(ConfigTest, TolerantOfBlankLinesAndTrailingSpaces) {
    const std::string body =
        "num-cpu 4   \n"
        "\n"
        "\n"
        "scheduler rr  \n"
        "quantum-cycles 5\n"
        "batch-process-freq 1\n"
        "min-ins 1\n"
        "max-ins 10\n"
        "delays-per-exec 0\n";
    EXPECT_NO_THROW(parseConfigFromString(body));
}
```

Use the exact symbol name (`parseConfigFromString` vs `parseConfig`) that already appears in `test_config.cpp`.

- [ ] **Step 2: Run; if CRLF test fails, fix the parser**

```bash
cmake --build build -j && ./build/mco1_tests --gtest_filter=ConfigTest.Parses*:ConfigTest.Tolerant*
```

If `ParsesCrlfLineEndings` fails, the value-side `\r` is leaking through. In `src/config/Config.cpp` around the line that extracts `value`, strip a trailing `\r`:

```cpp
        // strip stray CR after the value (CRLF on Windows-authored configs)
        if (!value.empty() && value.back() == '\r') value.pop_back();
```

- [ ] **Step 3: Commit**

```bash
git add tests/test_config.cpp src/config/Config.cpp
git commit -m "test+fix(config): tolerate CRLF and aggressive whitespace"
```

---

### Task 9: Console robustness to empty / whitespace-heavy input

**Spec cited:** GP §1 (CLI commands); CM CLI commands. Spec does not list empty input as a valid command; the REPL must ignore it (no crash, no error).

**Files:**
- Create: `tests/test_console.cpp`
- Modify: `CMakeLists.txt` (add the new test source)
- Possibly modify: `include/cli/Console.h` (expose a `handleLine` test seam if not already public)

- [ ] **Step 1: Check whether `Console::handleLine` (or equivalent dispatch entry) is callable from a test**

Run: `grep -n "handleLine\|dispatch\|class Console" include/cli/Console.h`
Expected: either a public method exists, or one needs to be exposed.

If the dispatch entry is private, change its visibility to public (it is already pure logic with no side-effects beyond `cout`). Do not add a new method; promote the existing one.

- [ ] **Step 2: Add the new test file**

Create `tests/test_console.cpp`:

```cpp
#include <gtest/gtest.h>
#include <sstream>
#include "cli/Console.h"

// REPL must ignore empty and whitespace-only input without throwing.
TEST(ConsoleTest, EmptyAndWhitespaceInputIsIgnored) {
    Console c;
    EXPECT_NO_THROW(c.handleLine(""));
    EXPECT_NO_THROW(c.handleLine("    "));
    EXPECT_NO_THROW(c.handleLine("\t\t  \t"));
}

// Excess internal whitespace must still tokenize to the same command.
TEST(ConsoleTest, ExcessWhitespaceTokenizesCleanly) {
    Console c;
    // Pre-initialize state: 'exit' is the only command allowed pre-initialize
    // per CM ("No command other than `exit` is valid before `initialize`").
    EXPECT_NO_THROW(c.handleLine("    exit   "));
}
```

If `Console` is not default-constructible (e.g. needs a `std::istream&`/`std::ostream&`), match the actual ctor in `include/cli/Console.h`. Do **not** invent a new ctor.

- [ ] **Step 3: Wire into CMake**

In `CMakeLists.txt`, inside the `add_executable(mco1_tests ...)` block, add `tests/test_console.cpp` to the source list.

- [ ] **Step 4: Run and commit**

```bash
cmake -S . -B build && cmake --build build -j
./build/mco1_tests --gtest_filter=ConsoleTest.*
git add CMakeLists.txt tests/test_console.cpp include/cli/Console.h
git commit -m "test(console): tolerate empty + whitespace-heavy input (GP:CLI)"
```

---

### Task 10: Concurrent snapshot stress test

**Spec cited:** DS line 418 — *"Snapshots copy under lock, return by value."* Edge case 5 (Task 3).

**Files:**
- Modify: `tests/test_scheduler.cpp`

- [ ] **Step 1: Add the stress test**

Append to `tests/test_scheduler.cpp`:

```cpp
// DS:418 — snapshots must remain race-free while CPUs mutate state.
TEST(SchedulerFixture, ConcurrentSnapshotsDuringHeavyTraffic) {
    SchedulerConfig cfg;
    cfg.numCpu = 4;
    cfg.algo = SchedulerConfig::Algo::RR;
    cfg.quantum = 2;
    cfg.delaysPerExec = 0;

    auto factory = [](uint32_t pid, std::string name) {
        auto p = std::make_unique<MockProcess>(pid, std::move(name));
        p->setTotalInstructions(20);
        return p;
    };

    Scheduler::instance().initialize(cfg, factory);
    Scheduler::instance().startGenerator();

    std::atomic<bool> stop{false};
    std::thread reader([&] {
        while (!stop.load()) {
            (void)Scheduler::instance().snapshotRunning();
            (void)Scheduler::instance().snapshotFinished();
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    stop.store(true);
    reader.join();

    Scheduler::instance().stopGenerator();
    Scheduler::instance().shutdown();
    SUCCEED();  // surviving without TSAN/segfault is the assertion
}
```

If the `factory` callback signature differs, match the existing fixture in `test_scheduler.cpp`. Do not invent a new shape.

- [ ] **Step 2: Run and commit**

```bash
cmake --build build -j && ./build/mco1_tests --gtest_filter=SchedulerFixture.ConcurrentSnapshots*
git add tests/test_scheduler.cpp
git commit -m "test: concurrent snapshot stress under heavy traffic (DS:418)"
```

---

### Task 11: Final verification

- [ ] **Step 1: Full clean build + test run**

```bash
rm -rf build && cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/mco1_tests --gtest_brief=1
```

Expected: all targets build clean; total test count is `33 + N` where `N` is the number of new test cases added across Tasks 2–10. No FAIL lines.

- [ ] **Step 2: Visual demo regression**

```bash
./build/scheduler_demo
```

Expected: the demo runs FCFS then RR sections to completion (matches commit `3a306c5` behavior).

- [ ] **Step 3: REPL smoke test**

```bash
printf "initialize\ndemo\nexit\n" | ./build/mco1
```

Expected: REPL prints initialization summary, runs the demo, exits cleanly.

---

## Self-Review

**1. Spec coverage:**
- Code review obs #1 → Task 2 (cites DS:417).
- Code review obs #2 (quantum-zero defensive code) → already correct per spec; no task needed.
- Code review obs #3 (sleep pool design) → already correct per spec; no task needed.
- Edge cases T1.1 (quantum + completion sync) → Task 5 (cites CR:139).
- Edge cases T1.2 (extreme quantum) → Task 4 (cites CR:134).
- Edge cases T1.3 (zero-length processes) → out of spec (CM min-ins range `[1,∞)`); explicitly excluded above.
- Edge cases T1.4 (simultaneous awakenings) → Task 6.
- Edge cases T1.5 (10k-process starvation) → no spec requirement; explicitly excluded.
- Edge cases T3.1 (128 cores) → Task 7 (cites CM upper bound).
- Edge cases T3.2 (extreme delays-per-exec) → Task 3 (cites GP:193 + the int-overflow defect).
- Edge cases T3.3 (config parsing oddities) → Task 8.
- Edge cases T3.4 (REPL edge cases) → Task 9.
- Edge cases T3.5 (async report-util) → Task 10 (cites DS:418).
- Plus: CLAUDE.md spec-adherence rule → Task 1 (user-requested).

**2. Placeholder scan:** none — every task has concrete code, exact file paths, exact build/test commands, and explicit spec citations. Where a method name might differ between code and plan, the step says "match the existing symbol in file X" rather than inventing.

**3. Type consistency:**
- `tickSleepHook` declared `std::function<void()>` in Task 2 and invoked as a nullary lambda there. Consistent.
- `delayTicksRemaining_` widened to `uint64_t` in Task 3; the assignment from `uint32_t delaysPerExec_` is then a safe widening conversion. Consistent.
- `adoptSleeping(std::unique_ptr<IProcess>)` introduced in Task 2 and reused in Task 6 with the same signature. Consistent.
- Test fixture name `SchedulerFixture` (matches `test_scheduler.cpp` existing convention) used in Tasks 7 and 10. Consistent.
- `EngineTest` used for `test_engine.cpp` cases (matches existing). Consistent.

---

## Notes for the executing engineer

- **Strict spec adherence.** Every assertion in every new test must trace back to a citation above. If a test seems to make sense but you cannot cite a spec line for it, **do not write it** — ask first.
- **No new commands, no new instructions, no new config keys.** Tracks 2 and 4 are out of scope.
- **One commit per task.** Don't bundle. Don't skip the failing-test step on Tasks 2 and 3 — the failing run is part of the proof.
- **MockProcess only.** Do not introduce a real Process implementation; that's Track 2.
