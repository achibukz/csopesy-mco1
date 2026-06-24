# Track 1 Q3 Fixes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make Q3 of the MCO1 quiz record correctly — `screen -ls` after `scheduler-start` + 5 s wait must show **CPU utilization: 100.00%**, **Cores used: N** (where N = `num-cpu`), and N running processes with distinct, accurate Core IDs `0..N-1`.

**Architecture:** Two small targeted fixes inside Track 1 only. (1) `SchedulerEngine::runningProcs_` becomes a per-core indexed slot vector (size = `numCpu`, `nullptr` for idle cores) so `markRunning` honors the `coreId` it already receives. (2) `CPU::run` no longer wastes a tick after a process exits / sleeps / is preempted — it immediately tries `popReady` within the same tick iteration, eliminating the per-quantum idle gap that drives utilization snapshots below 100%.

**Tech Stack:** C++20, GoogleTest, CMake. POSIX threads via `std::thread`. No external libraries added.

---

## Scope Discipline (read first)

- **Edit ONLY Track 1 sources** — i.e. `src/scheduler/*`, `include/scheduler/*`, `tests/test_engine.cpp`, `tests/test_scheduler.cpp`, `tests/test_policy.cpp`, `tests/MockProcess.*`.
- **Do not edit** `src/cli/*`, `src/process/*`, `include/cli/*`, `include/process/*`. If a defect there is implicated, write it into the *Track 2/4 Notes* section at the bottom of this plan and stop.
- **Minimum diff principle.** Prefer the smallest viable change that passes the goal. Do not refactor surrounding code, rename symbols, restructure files, or "clean up" while in here.
- **Spec authority:** `docs/MO1 - OS Emulator - Process Scheduler.docx`. The quiz mockup for Q3 (image in chat) is the visual target.

## What this plan does NOT change

- Public interface of `SchedulerEngine` (no method renames, no signature additions, no signature removals).
- Public interface of `Scheduler`.
- `IProcess` interface.
- `SchedulingPolicy` (FCFS / RR) logic.
- Anything in `src/cli/` (ScreenManager, Reporter, Console) or `src/process/`.

## Existing tests inventory (must keep passing)

Audit done on 2026-06-24 at the head of branch `Screen-Multiplexer-+-Reporting`:

- `tests/test_engine.cpp` — 11 tests (engine bookkeeping, generator, sleep ticking, lock discipline, overflow guard). 3 assertions reference `snapshotRunning().size()`; lines 87, 91, 121.
- `tests/test_scheduler.cpp` — 11 tests (FCFS, RR, delays, sleep, generator, 128 cores, concurrent snapshots, quantum-boundary finish, shutdown bound). One assertion at line 215 references `getRunningSnapshot().size() == 0u`.
- `tests/test_policy.cpp` — RR/FCFS policy unit tests. **No assertion touches the per-core indexing.**
- `tests/test_process.cpp`, `tests/test_console.cpp`, `tests/test_config.cpp` — irrelevant to this change.

The three `test_engine` lines and one `test_scheduler` line above are the only existing tests that will break under B1's new snapshot semantics. They are updated in Tasks 5 and 6.

## File Structure

| File | Action | Why |
|------|--------|-----|
| `include/scheduler/SchedulerEngine.h` | No change (member type stays `std::vector<IProcess*>`) | Semantics shift, type unchanged. |
| `src/scheduler/SchedulerEngine.cpp` | Modify constructor + 5 mutator methods + (optionally) `snapshotRunning` | B1: per-core slot indexing. |
| `src/scheduler/CPU.cpp` | Modify `CPU::run()` only | B2: no-tick-gap after release. |
| `tests/test_engine.cpp` | Update 2 existing tests, add 2 new tests | New per-core semantics + regression for re-acquire-in-same-tick (where testable). |
| `tests/test_scheduler.cpp` | Update 1 existing test line + add 1 new test | Snapshot size assertion + steady-state utilization. |

No new files created. No deletions.

---

## Task 1: Snapshot baseline — confirm green tests before any change

**Files:** none modified.

- [ ] **Step 1: Build clean**

Run: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug`
Run: `cmake --build build -j`
Expected: build succeeds with no warnings escalated to errors.

- [ ] **Step 2: Run full test suite as baseline**

Run: `ctest --test-dir build --output-on-failure`
Expected: all tests pass. Record the exact count printed at the end (e.g. `100% tests passed, X tests`) — that count must still pass at the end of Task 11.

- [ ] **Step 3: Commit nothing, take note**

No commit. Record baseline test count in your scratch notes.

---

## Task 2: Add failing test that proves `markRunning` honors `coreId`

**Files:**
- Modify: `tests/test_engine.cpp` (append at end, before final `}`)

- [ ] **Step 1: Add the failing test**

Append this test to `tests/test_engine.cpp` (after the last existing test, before any closing namespace if present — match the surrounding pattern of `TEST(EngineTest, ...)`):

```cpp
// Q3 fix B1: markRunning must place the process at the slot indicated by
// coreId so the snapshot's vector index equals the real CPU core.
TEST(EngineTest, MarkRunningPlacesProcessAtCoreIdSlot) {
    FCFSPolicy policy;
    SchedulerConfig cfg = makeConfig();
    cfg.numCpu = 4;
    SchedulerEngine engine(cfg, policy);

    MockProcess a(1, "a", 5);
    MockProcess b(2, "b", 5);

    engine.markRunning(&a, 2);  // core 2
    engine.markRunning(&b, 0);  // core 0

    auto running = engine.snapshotRunning();
    ASSERT_EQ(running.size(), 4u) << "snapshot must be sized to numCpu with nullptr for idle cores";
    EXPECT_EQ(running[0], &b);
    EXPECT_EQ(running[1], nullptr);
    EXPECT_EQ(running[2], &a);
    EXPECT_EQ(running[3], nullptr);
    EXPECT_EQ(engine.coresUsed(), 2);
}
```

- [ ] **Step 2: Run the new test to confirm it fails**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure -R MarkRunningPlacesProcessAtCoreIdSlot`
Expected: FAIL. The current `snapshotRunning()` returns size 2 (insertion order `[&a, &b]`), so `ASSERT_EQ(running.size(), 4u)` fires.

- [ ] **Step 3: Do not commit yet** — implementation follows in Tasks 3–5.

---

## Task 3: Implement per-core slot in `SchedulerEngine` constructor + `markRunning`

**Files:**
- Modify: `src/scheduler/SchedulerEngine.cpp`

- [ ] **Step 1: Initialize `runningProcs_` to `numCpu` nullptr slots in the constructor**

Find the `SchedulerEngine` constructor (top of `src/scheduler/SchedulerEngine.cpp`). It currently looks roughly like:

```cpp
SchedulerEngine::SchedulerEngine(const SchedulerConfig& cfg, ISchedulingPolicy& policy)
    : cfg_(cfg), policy_(policy) {}
```

Update the body so `runningProcs_` is sized to `cfg_.numCpu` with `nullptr`:

```cpp
SchedulerEngine::SchedulerEngine(const SchedulerConfig& cfg, ISchedulingPolicy& policy)
    : cfg_(cfg), policy_(policy) {
    runningProcs_.assign(cfg_.numCpu, nullptr);
}
```

(If the constructor body already exists with other statements, add the `runningProcs_.assign(...)` line at the end of the body.)

- [ ] **Step 2: Rewrite `markRunning` to index by `coreId`**

Find `void SchedulerEngine::markRunning(IProcess* p, int /*coreId*/)` (around line 152). Replace the entire function body with:

```cpp
void SchedulerEngine::markRunning(IProcess* p, int coreId) {
    if (!p) return;
    if (coreId < 0 || coreId >= static_cast<int>(runningProcs_.size())) return;
    std::lock_guard<std::mutex> lk(stateMutex_);
    if (runningProcs_[coreId] == p) return;        // idempotent
    if (runningProcs_[coreId] == nullptr) {
        ++coresUsed_;
    }
    runningProcs_[coreId] = p;
}
```

Notes for the implementer:
- Bounds-check on `coreId` is defensive — production callers (`CPU::run`) always pass a valid `coreId_`, but a stray test or future change shouldn't corrupt memory.
- `++coresUsed_` only fires when transitioning from `nullptr → p`. If a slot already held some other process (real schedulers should never do this), we overwrite without double-counting.

- [ ] **Step 3: Run the Task 2 test**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure -R MarkRunningPlacesProcessAtCoreIdSlot`
Expected: PASS.

- [ ] **Step 4: Do not commit yet** — other mutators still use the old find-and-erase logic; some existing tests will fail on size assertions. Tasks 4–6 finish the rewrite.

---

## Task 4: Rewrite `clearRunning`, `markFinished`, `moveToSleeping`, `preempt` for per-core slots

**Files:**
- Modify: `src/scheduler/SchedulerEngine.cpp`

These four functions currently use `std::find(...) + erase(...)`. They need to instead find the slot that holds `p`, null it out, and decrement `coresUsed_`. The follow-up behavior (push to finished / sleeping / call policy.onPreempt) stays identical.

- [ ] **Step 1: Rewrite `clearRunning`**

Replace:

```cpp
void SchedulerEngine::clearRunning(IProcess* p) {
    if (!p) return;
    std::lock_guard<std::mutex> lk(stateMutex_);
    auto it = std::find(runningProcs_.begin(), runningProcs_.end(), p);
    if (it != runningProcs_.end()) {
        runningProcs_.erase(it);
        --coresUsed_;
    }
}
```

with:

```cpp
void SchedulerEngine::clearRunning(IProcess* p) {
    if (!p) return;
    std::lock_guard<std::mutex> lk(stateMutex_);
    for (auto& slot : runningProcs_) {
        if (slot == p) {
            slot = nullptr;
            --coresUsed_;
            return;
        }
    }
}
```

- [ ] **Step 2: Rewrite `markFinished`**

Replace:

```cpp
void SchedulerEngine::markFinished(IProcess* p) {
    if (!p) return;
    std::lock_guard<std::mutex> lk(stateMutex_);
    auto it = std::find(runningProcs_.begin(), runningProcs_.end(), p);
    if (it != runningProcs_.end()) {
        runningProcs_.erase(it);
        --coresUsed_;
    }
    finishedProcs_.push_back(p);
}
```

with:

```cpp
void SchedulerEngine::markFinished(IProcess* p) {
    if (!p) return;
    std::lock_guard<std::mutex> lk(stateMutex_);
    for (auto& slot : runningProcs_) {
        if (slot == p) {
            slot = nullptr;
            --coresUsed_;
            break;
        }
    }
    finishedProcs_.push_back(p);
}
```

- [ ] **Step 3: Rewrite `preempt`**

Replace:

```cpp
void SchedulerEngine::preempt(IProcess* p, ISchedulingPolicy& policy) {
    if (!p) return;
    std::lock_guard<std::mutex> lk(stateMutex_);
    auto it = std::find(runningProcs_.begin(), runningProcs_.end(), p);
    if (it != runningProcs_.end()) {
        runningProcs_.erase(it);
        --coresUsed_;
    }
    policy.onPreempt(p, ready_);
}
```

with:

```cpp
void SchedulerEngine::preempt(IProcess* p, ISchedulingPolicy& policy) {
    if (!p) return;
    std::lock_guard<std::mutex> lk(stateMutex_);
    for (auto& slot : runningProcs_) {
        if (slot == p) {
            slot = nullptr;
            --coresUsed_;
            break;
        }
    }
    policy.onPreempt(p, ready_);
}
```

- [ ] **Step 4: Rewrite `moveToSleeping`**

Replace:

```cpp
void SchedulerEngine::moveToSleeping(IProcess* p) {
    if (!p) return;
    std::lock_guard<std::mutex> lk(stateMutex_);
    auto it = std::find(runningProcs_.begin(), runningProcs_.end(), p);
    if (it != runningProcs_.end()) {
        runningProcs_.erase(it);
        --coresUsed_;
    }
    sleepingProcs_.push_back(p);
}
```

with:

```cpp
void SchedulerEngine::moveToSleeping(IProcess* p) {
    if (!p) return;
    std::lock_guard<std::mutex> lk(stateMutex_);
    for (auto& slot : runningProcs_) {
        if (slot == p) {
            slot = nullptr;
            --coresUsed_;
            break;
        }
    }
    sleepingProcs_.push_back(p);
}
```

(If your local `moveToSleeping` body differs, preserve the existing post-find behavior — only swap the find+erase for the slot-null loop.)

- [ ] **Step 5: Build**

Run: `cmake --build build -j`
Expected: builds clean.

- [ ] **Step 6: Do not commit yet** — tests in Task 5 will likely fail; fix them next.

---

## Task 5: Update existing `test_engine.cpp` assertions to match new semantics

**Files:**
- Modify: `tests/test_engine.cpp` lines 80–123 (three tests).

The three failing tests assert `.size() == 0u` (running snapshot is "empty") or `.size() == 1u` after one `markRunning`. Under per-core semantics, the snapshot is always size `numCpu`. Re-express each assertion in terms of `coresUsed()` and / or `nullptr` content — these reflect the same intent.

- [ ] **Step 1: Fix `RunningAndFinishedSnapshotsAreCopies` (around line 80)**

Replace:

```cpp
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
```

with:

```cpp
TEST(EngineTest, RunningAndFinishedSnapshotsAreCopies) {
    FCFSPolicy policy;
    SchedulerEngine engine(makeConfig(), policy);  // numCpu = 1
    MockProcess a(1, "a", 5);
    engine.markRunning(&a, 0);

    auto running = engine.snapshotRunning();
    EXPECT_EQ(running.size(), 1u);          // numCpu = 1
    EXPECT_EQ(running[0], &a);
    EXPECT_EQ(engine.coresUsed(), 1);

    engine.markFinished(&a);
    auto runningAfter = engine.snapshotRunning();
    EXPECT_EQ(runningAfter.size(), 1u);     // still sized numCpu
    EXPECT_EQ(runningAfter[0], nullptr);
    EXPECT_EQ(engine.coresUsed(), 0);
    EXPECT_EQ(engine.snapshotFinished().size(), 1u);
    EXPECT_EQ(running.size(), 1u);          // original snapshot is an independent copy
}
```

- [ ] **Step 2: Fix `CoresUsedTracksRunning` (around line 96)**

This test uses `numCpu=1` then `markRunning(&a,0)` followed by `markRunning(&b,0)` — under per-core semantics that would overwrite slot 0 not add. Use a 2-core config and distinct coreIds.

Replace:

```cpp
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
```

with:

```cpp
TEST(EngineTest, CoresUsedTracksRunning) {
    FCFSPolicy policy;
    SchedulerConfig cfg = makeConfig();
    cfg.numCpu = 2;
    SchedulerEngine engine(cfg, policy);
    MockProcess a(1, "a", 5);
    MockProcess b(2, "b", 5);

    EXPECT_EQ(engine.coresUsed(), 0);
    engine.markRunning(&a, 0);
    EXPECT_EQ(engine.coresUsed(), 1);
    engine.markRunning(&b, 1);
    EXPECT_EQ(engine.coresUsed(), 2);
    engine.clearRunning(&a);
    EXPECT_EQ(engine.coresUsed(), 1);
    engine.markFinished(&b);
    EXPECT_EQ(engine.coresUsed(), 0);
}
```

- [ ] **Step 3: Fix `PreemptRemovesFromRunningAndCallsPolicy` (around line 113)**

Replace:

```cpp
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
```

with:

```cpp
TEST(EngineTest, PreemptRemovesFromRunningAndCallsPolicy) {
    RRPolicy policy;
    SchedulerEngine engine(makeConfig(), policy);  // numCpu = 1
    MockProcess a(1, "a", 5);
    engine.markRunning(&a, 0);

    engine.preempt(&a, policy);
    EXPECT_EQ(engine.coresUsed(), 0);
    auto running = engine.snapshotRunning();
    EXPECT_EQ(running.size(), 1u);            // numCpu = 1
    EXPECT_EQ(running[0], nullptr);
    EXPECT_EQ(engine.popReady(), &a);
}
```

- [ ] **Step 4: Run engine tests**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure -R EngineTest`
Expected: all `EngineTest.*` tests pass, including the new `MarkRunningPlacesProcessAtCoreIdSlot`.

- [ ] **Step 5: Do not commit yet** — `test_scheduler.cpp` line 215 may still fail; fix in Task 6.

---

## Task 6: Update `test_scheduler.cpp` snapshot size assertion + add steady-state regression

**Files:**
- Modify: `tests/test_scheduler.cpp` line 215.
- Modify: `tests/test_scheduler.cpp` (append new test).

- [ ] **Step 1: Fix the size assertion in `RRFinishOnQuantumBoundaryGoesToFinishedNotReady` (line 215)**

Change:

```cpp
    EXPECT_EQ(Scheduler::instance().getRunningSnapshot().size(), 0u);
```

to:

```cpp
    auto running = Scheduler::instance().getRunningSnapshot();
    int activeCores = 0;
    for (auto* proc : running) if (proc) ++activeCores;
    EXPECT_EQ(activeCores, 0);
```

- [ ] **Step 2: Add a regression test for B2 (no-tick-gap utilization)**

Append at the end of `tests/test_scheduler.cpp`, just before the final closing brace if any:

```cpp
// Q3 fix B2: under continuous scheduler-start with batch-freq=1 and a deep
// ready queue, the CPU loop must not waste a tick after finish/sleep/preempt.
// Sample coresUsed across the run; the average must be very close to numCpu.
TEST_F(SchedulerFixture, SteadyStateUtilizationNearOneHundredPercent) {
    auto cfg = makeCfg(/*cores=*/4, SchedulerConfig::Algo::RR,
                       /*quantum=*/5, /*delays=*/0, /*batch=*/1);
    Scheduler::instance().initialize(cfg);
    Scheduler::instance().setProcessFactory([](const std::string& name, int pid) {
        return std::make_unique<MockProcess>(pid, name, 200);
    });
    Scheduler::instance().startGenerator();

    // Warm up so the ready queue has plenty of work.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Sample coresUsed for 200 ms. We expect average > 90% (i.e. > 3.6 of 4).
    int samples = 0;
    int sumUsed = 0;
    auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
    while (std::chrono::steady_clock::now() < until) {
        sumUsed += Scheduler::instance().getCoresUsed();
        ++samples;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    Scheduler::instance().stopGenerator();
    ASSERT_GT(samples, 0);
    double avg = static_cast<double>(sumUsed) / samples;
    EXPECT_GT(avg, 3.6) << "Average coresUsed " << avg << " — CPUs are wasting ticks between processes.";
}
```

Notes:
- 200 ticks per `MockProcess` × `batch=1` × 4 cores means the ready queue is deep enough never to empty during the sample window.
- Threshold `3.6` (= 90 % of 4 cores) is conservative. Before B2 the value was ≈ 3.0 (75 %). After B2 it should be ≥ 3.9.

- [ ] **Step 3: Build and run only the new test to confirm it FAILS pre-B2**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure -R SteadyStateUtilizationNearOneHundredPercent`
Expected: **FAIL** with the average around 3.0. This proves B2 is needed.

- [ ] **Step 4: Do not commit yet** — B2 implementation follows.

---

## Task 7: Implement B2 — `CPU::run` re-acquires within the same tick

**Files:**
- Modify: `src/scheduler/CPU.cpp` lines 27–71 (the entire `void CPU::run()`).

- [ ] **Step 1: Replace `CPU::run` with the re-acquire variant**

Replace the entire function:

```cpp
void CPU::run() {
    while (running_.load()) {
        engine_.waitForNextTick(lastSeenTick_);
        if (!running_.load()) break;
        if (!engine_.isRunning()) break;

        IProcess* p = current_.load();

        if (!p) {
            p = engine_.popReady();
            if (!p) continue;
            engine_.markRunning(p, coreId_);
            current_.store(p);
            ticksOnCurrent_ = 0;
            delayTicksRemaining_ = 0;
        }

        if (delayTicksRemaining_ > 0) {
            --delayTicksRemaining_;
            ++ticksOnCurrent_;
            continue;
        }

        p->executeNext(engine_.currentTick());
        delayTicksRemaining_ = delaysPerExec_;
        ++ticksOnCurrent_;

        if (p->isFinished()) {
            engine_.markFinished(p);
            current_.store(nullptr);
            continue;
        }

        if (p->getState() == ProcessState::WAITING) {
            engine_.moveToSleeping(p);
            current_.store(nullptr);
            continue;
        }

        if (!policy_.shouldKeepRunning(p, ticksOnCurrent_, quantum_)) {
            engine_.preempt(p, policy_);
            current_.store(nullptr);
        }
    }
}
```

with:

```cpp
void CPU::run() {
    while (running_.load()) {
        engine_.waitForNextTick(lastSeenTick_);
        if (!running_.load()) break;
        if (!engine_.isRunning()) break;

        IProcess* p = current_.load();

        if (!p) {
            p = engine_.popReady();
            if (!p) continue;
            engine_.markRunning(p, coreId_);
            current_.store(p);
            ticksOnCurrent_ = 0;
            delayTicksRemaining_ = 0;
        }

        if (delayTicksRemaining_ > 0) {
            --delayTicksRemaining_;
            ++ticksOnCurrent_;
            continue;
        }

        p->executeNext(engine_.currentTick());
        delayTicksRemaining_ = delaysPerExec_;
        ++ticksOnCurrent_;

        bool releasedCore = false;

        if (p->isFinished()) {
            engine_.markFinished(p);
            current_.store(nullptr);
            releasedCore = true;
        } else if (p->getState() == ProcessState::WAITING) {
            engine_.moveToSleeping(p);
            current_.store(nullptr);
            releasedCore = true;
        } else if (!policy_.shouldKeepRunning(p, ticksOnCurrent_, quantum_)) {
            engine_.preempt(p, policy_);
            current_.store(nullptr);
            releasedCore = true;
        }

        if (releasedCore) {
            IProcess* q = engine_.popReady();
            if (q) {
                engine_.markRunning(q, coreId_);
                current_.store(q);
                ticksOnCurrent_ = 0;
                delayTicksRemaining_ = 0;
            }
        }
    }
}
```

Why this is safe:
- The new process `q` is NOT executed on this tick — only loaded. `executeNext` runs once per loop iteration. The next tick's iteration will start with `current_ == q` (non-null), skip the empty-`p` branch, and execute `q` exactly once. This preserves the spec rule of *one instruction per CPU per tick*.
- The `delaysPerExec_` busy-wait counter is reset to 0 for the new process — its first execution next tick is immediate, which matches the existing behavior of the empty-`p` branch.
- RR's `onPreempt` already pushes the released process to the back of `ready_`. Calling `popReady` right after will get a different process (FIFO), so the same process isn't re-grabbed.
- FCFS never preempts mid-run, so the `else if (!policy_.shouldKeepRunning(...))` branch is effectively RR-only; FCFS only releases on finish or sleep.

- [ ] **Step 2: Build**

Run: `cmake --build build -j`
Expected: builds clean.

- [ ] **Step 3: Run the B2 regression test**

Run: `ctest --test-dir build --output-on-failure -R SteadyStateUtilizationNearOneHundredPercent`
Expected: PASS. Average coresUsed should be ≥ 3.9.

---

## Task 8: Run the full test suite

**Files:** none.

- [ ] **Step 1: Full ctest**

Run: `ctest --test-dir build --output-on-failure`
Expected: every test passes — including all `EngineTest.*`, all `SchedulerFixture.*` (FCFS, RR, delays, sleep, generator, 128 cores, concurrent snapshots, quantum-boundary finish, shutdown bound), policy tests, process tests, console tests, config tests.

- [ ] **Step 2: If anything fails, do NOT proceed**

Triage: the only legitimate failures at this point are tests that asserted the old vector semantics. Tasks 5 and 6 should already cover them; if a fourth surfaces, update it the same way (re-express in `coresUsed()` terms) — do not relax B1 or B2 semantics.

- [ ] **Step 3: Run a stress repeat to flush timing flakes**

Run: `for i in 1 2 3 4 5; do ctest --test-dir build --output-on-failure -R SchedulerFixture || break; done`
Expected: all 5 iterations pass. If `SteadyStateUtilizationNearOneHundredPercent` fails intermittently, raise the warm-up sleep from 50 ms to 100 ms before declaring the change broken — it must NOT be flaky on a quiet machine.

---

## Task 9: Manual Q3 reproduction

**Files:**
- Modify: `config.txt` (revert after).

- [ ] **Step 1: Set Q3 config**

Write `config.txt` with exactly:

```
num-cpu 4
scheduler rr
quantum-cycles 5
batch-process-freq 1
min-ins 1000
max-ins 2000
delays-per-exec 0
```

- [ ] **Step 2: Build and run the emulator**

Run: `cmake --build build -j && ./build/mco1`
At the prompt:
1. Type `initialize` → expect config loaded.
2. Type `scheduler-start` → expect "Generator started." (or similar).
3. Wait 5 seconds.
4. Type `screen -ls`.

- [ ] **Step 3: Verify expected output**

The output must show:
- `CPU utilization: 100.00%` (acceptable: 99.5 % – 100.0 %)
- `Cores used: 4`
- `Cores available: 0 / 4`
- A `Running processes:` section with **4** entries
- Each entry has a unique `Core: 0`, `Core: 1`, `Core: 2`, `Core: 3` (one per core, no duplicates, no skips)

If utilization comes in under 99.5 %: B2 didn't take effect; re-check `CPU::run` changes.
If Core IDs are duplicated or skipped: B1 didn't take effect; re-check `markRunning`/`runningProcs_` init.

- [ ] **Step 4: Stop and exit cleanly**

Type `scheduler-stop` then `exit`. The program must terminate without hang or crash.

- [ ] **Step 5: Restore default `config.txt`**

`git checkout -- config.txt`

---

## Task 10: Q4 regression check (must still pass)

**Files:**
- Modify: `config.txt` (revert after).

- [ ] **Step 1: Set Q4 config**

Write `config.txt`:

```
num-cpu 32
scheduler rr
quantum-cycles 1
batch-process-freq 2
min-ins 100
max-ins 100
delays-per-exec 0
```

- [ ] **Step 2: Run the Q4 sequence**

Run: `./build/mco1`
At the prompt:
1. `initialize`
2. `scheduler-start`
3. Wait 10 seconds.
4. `scheduler-stop`
5. Wait 30 seconds.
6. `screen -ls`
7. `report-util`

- [ ] **Step 3: Verify expected output**

- `CPU utilization: 0.00%`
- `Cores used: 0`
- `Running processes:` shows `None`
- `Finished processes:` shows many entries
- `csopesy-log.txt` was appended with the same data

This must NOT regress.

- [ ] **Step 4: Restore `config.txt`**

`git checkout -- config.txt`

---

## Task 11: Atomic commit

**Files:** all changes from Tasks 3–7.

- [ ] **Step 1: Confirm only Track 1 files changed**

Run: `git status`
Expected files listed (and only these):

```
modified:   src/scheduler/SchedulerEngine.cpp
modified:   src/scheduler/CPU.cpp
modified:   tests/test_engine.cpp
modified:   tests/test_scheduler.cpp
```

If anything else appears, revert it. NO changes to `src/cli/*`, `src/process/*`, `include/cli/*`, `include/process/*` are permitted by this plan.

- [ ] **Step 2: Stage and review the diff**

Run: `git add src/scheduler/SchedulerEngine.cpp src/scheduler/CPU.cpp tests/test_engine.cpp tests/test_scheduler.cpp`
Run: `git diff --cached`
Skim the diff once. Confirm: no rename refactors, no comment cleanups beyond what's strictly required, no accidental whitespace churn.

- [ ] **Step 3: Commit**

```bash
git commit -m "$(cat <<'EOF'
fix(scheduler): per-core slot tracking + no-tick-gap CPU loop (Track 1 Q3)

- SchedulerEngine::runningProcs_ is now sized to numCpu and indexed by
  coreId, so markRunning honors the core argument it already receives.
  snapshotRunning() returns the per-core view (nullptr for idle slots);
  Reporter's existing Core-index display now reflects real core IDs.
- CPU::run() immediately popReady() after finish/sleep/preempt so the
  released core is re-loaded within the same tick instead of waiting
  for the next tick. Drives steady-state CPU utilization to ~100%.
- Tests: added MarkRunningPlacesProcessAtCoreIdSlot and
  SteadyStateUtilizationNearOneHundredPercent; updated three existing
  assertions that expected the compacted-vector snapshot semantics.
EOF
)"
```

- [ ] **Step 4: Final verification**

Run: `ctest --test-dir build --output-on-failure`
Expected: still green.

Run: `git log -1 --stat`
Confirm the commit touches only the four files listed above.

---

## Self-Review (run before declaring done)

1. **Spec coverage:** Q3 expected output is "100% CPU utilization printed in the screen-ls command and mostly running processes." → Task 9 verifies. The Q3 mockup shows distinct Core IDs `0..3` → Tasks 3–5 enforce this. ✓
2. **Placeholder scan:** No `TODO`, `TBD`, "implement later" in the plan. Every code block is complete. ✓
3. **Type consistency:** `markRunning(IProcess*, int)`, `snapshotRunning()` returning `std::vector<IProcess*>`, `coresUsed()` returning `int` — all match across tasks. ✓
4. **Test-first discipline:** Both fixes have failing tests added before the implementation (Tasks 2 → 3 for B1; Task 6 → 7 for B2). ✓
5. **Existing test inventory:** All four breaking assertions (`test_engine` lines 87, 91, 121; `test_scheduler` line 215) are explicitly updated. ✓

---

## Track 2 / Track 4 Notes (DO NOT FIX IN THIS PLAN — record for later)

These were observed while preparing Q3 but are out of scope.

- **Q5 — `screen -ls` doesn't show WAITING processes.** Root cause: `src/cli/Reporter.cpp` only renders `Running processes:` and `Finished processes:`. A user-created process that is in `WAITING` (sleeping) at snapshot time is invisible. Quiz Q5 explicitly says "screen -ls should list all 3 processes created." Track 4 fix needed — likely add a `Waiting processes:` section sourced from `Scheduler::getAllSnapshot()` filtered by state, or print every tracked process with a state label.

- **Q6 — preliminary deterministic generator.** Q6 requires every spawned process to have a symbol table seeded with `x=y=z=0` and a fixed instruction set `FOR([ADD(x,x,1), PRINT("Value from: "+x), ADD(y,y,1), PRINT("Value from: "+y), ADD(z,z,1), PRINT("Value from: "+z)], 100)`. Track 2 (`src/process/Instructions.cpp` / `Process.cpp`) needs a knob to swap the random generator for this fixed program for Q6 recording. Not Track 1.

- **`screen -s` does not clear the console.** Spec says "the console will clear its contents and 'move' to the process screen." `ScreenManager::showProcessScreen` just prints. Track 4 fix in `src/cli/ScreenManager.cpp`.

These three are tracked separately; this plan does not touch them.
