# Fix `tickSleepingProcesses` Rebuild-Phase Lock Violation — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Eliminate the documented locking-discipline violation in `SchedulerEngine::tickSleepingProcesses` where `p->getState()` is called under `stateMutex_` during the rebuild phase.

**Architecture:** Read each sleeper's post-`tickSleep()` state into a local `unordered_set` off-lock, then re-acquire the lock and use the set as a pure-data predicate to partition `sleepingProcs_` into "still sleeping" vs "push to ready". Locking rule from the design spec (line 422) and `CLAUDE.md` "Modularity rules" — *"Never hold `stateMutex_` across a process method call"* — is restored.

**Tech Stack:** C++20, GoogleTest 1.14 via FetchContent, CMake/CTest.

---

## Background

`SchedulerEngine::tickSleepingProcesses` (`src/scheduler/SchedulerEngine.cpp:195-223`) follows a three-phase locking pattern:

1. **Snapshot** sleepers under lock (correct).
2. **Tick** each sleeper off-lock (correct — verified by existing `EngineTest.TickSleepingDoesNotHoldStateMutex`).
3. **Rebuild** under lock — *incorrectly calls `p->getState()` while holding `stateMutex_`*.

`MockProcess::getState()` is currently a plain atomic load, so the existing regression test (which hooks `tickSleep`, not `getState`) does not catch step 3. We extend the test seam with a `getStateHook` analogous to `tickSleepHook`, write a failing test that proves the lock is held during step 3's `getState()` call, then refactor step 3 to read states off-lock.

## File Structure

- **Modify** `tests/MockProcess.h` — add `mutable std::function<void()> getStateHook;`
- **Modify** `tests/MockProcess.cpp` — move `getState()` out-of-line; fire hook before returning `state_.load()`
- **Modify** `tests/test_engine.cpp` — add `EngineTest.TickSleepingDoesNotHoldStateMutexDuringRebuild`
- **Modify** `src/scheduler/SchedulerEngine.cpp` — refactor step 3 of `tickSleepingProcesses`

No new files. No public API changes. No header additions outside `<unordered_set>` in one .cpp.

---

## Task 1: Extend MockProcess with `getStateHook` test seam

**Files:**
- Modify: `tests/MockProcess.h:27` (move `getState` out-of-line) and `:39` (declare new hook)
- Modify: `tests/MockProcess.cpp` (define out-of-line `getState`)

- [ ] **Step 1: Move `getState()` declaration out-of-line in `MockProcess.h`**

Replace line 27:

```cpp
    ProcessState  getState() const override       { return state_.load(); }
```

with:

```cpp
    ProcessState  getState() const override;
```

- [ ] **Step 2: Add `getStateHook` field to `MockProcess.h`**

After line 39 (`std::function<void()> tickSleepHook;`), add:

```cpp
    // Called inside getState() before returning state_. Used by tests to
    // detect whether the engine still holds stateMutex_ when reading process
    // state. Default: no-op.
    mutable std::function<void()> getStateHook;
```

- [ ] **Step 3: Define out-of-line `getState()` in `MockProcess.cpp`**

Add this definition near the other accessor definitions:

```cpp
ProcessState MockProcess::getState() const {
    if (getStateHook) getStateHook();
    return state_.load();
}
```

- [ ] **Step 4: Build and run the existing engine tests to confirm no regression**

Run: `cmake --build build -j && ctest --test-dir build -R EngineTest --output-on-failure`
Expected: all existing `EngineTest.*` tests PASS (including `TickSleepingDoesNotHoldStateMutex`).

- [ ] **Step 5: Commit**

```bash
git add tests/MockProcess.h tests/MockProcess.cpp
git commit -m "test: add getStateHook seam to MockProcess for lock-discipline tests"
```

---

## Task 2: Add failing regression test for rebuild-phase lock violation

**Files:**
- Modify: `tests/test_engine.cpp` (add new test after `TickSleepingDoesNotHoldStateMutex` at `:127-153`)

- [ ] **Step 1: Add the failing test**

Insert this test immediately after `TEST(EngineTest, TickSleepingDoesNotHoldStateMutex)` (after line 153):

```cpp
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
    ASSERT_EQ(sleeper->getState(), ProcessState::SLEEPING);

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
```

- [ ] **Step 2: Build the test binary**

Run: `cmake --build build -j --target mco1_tests`
Expected: build SUCCESS.

- [ ] **Step 3: Run the new test in isolation with a hard timeout (proves it deadlocks today)**

Run: `ctest --test-dir build -R TickSleepingDoesNotHoldStateMutexDuringRebuild --output-on-failure --timeout 10`
Expected: FAIL — test exceeds the 10-second timeout because `snapshotRunning()` blocks on the held `stateMutex_`. This is the red bar that proves the bug.

- [ ] **Step 4: Do NOT commit yet** — failing tests don't land alone. Proceed to Task 3.

---

## Task 3: Refactor `tickSleepingProcesses` to read states off-lock

**Files:**
- Modify: `src/scheduler/SchedulerEngine.cpp:195-223` (rewrite step 3), `:1-10` (add include)

- [ ] **Step 1: Add `<unordered_set>` include**

In `src/scheduler/SchedulerEngine.cpp`, after the existing `#include <algorithm>` (line 6), add:

```cpp
#include <unordered_set>
```

- [ ] **Step 2: Replace the body of `tickSleepingProcesses`**

Replace the current implementation (lines 195-223) with:

```cpp
void SchedulerEngine::tickSleepingProcesses() {
    // DS line 422: never hold stateMutex_ across a process method call.
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

    // 3) read post-tick states off-lock; collect woken pointers
    std::unordered_set<IProcess*> woken;
    for (IProcess* p : sleepers) {
        if (p->getState() != ProcessState::SLEEPING) {
            woken.insert(p);
        }
    }

    // 4) under lock, rebuild sleepingProcs_ from the engine's *current* list
    //    (preserving any sleepers added between steps 1 and 4) using the
    //    off-lock `woken` set as a pure-data predicate. No process method
    //    is called inside this critical section.
    std::lock_guard<std::mutex> lk(stateMutex_);
    std::vector<IProcess*> stillSleeping;
    stillSleeping.reserve(sleepingProcs_.size());
    for (IProcess* p : sleepingProcs_) {
        if (woken.count(p)) {
            ready_.push(p);
        } else {
            stillSleeping.push_back(p);
        }
    }
    sleepingProcs_.swap(stillSleeping);
}
```

- [ ] **Step 3: Build**

Run: `cmake --build build -j`
Expected: build SUCCESS with `-Wall -Wextra -Wpedantic` clean.

- [ ] **Step 4: Run the new regression test — should now PASS**

Run: `ctest --test-dir build -R TickSleepingDoesNotHoldStateMutexDuringRebuild --output-on-failure --timeout 10`
Expected: PASS in well under 1 second.

- [ ] **Step 5: Run the full test suite to confirm no behavioral regression**

Run: `ctest --test-dir build --output-on-failure`
Expected: all tests PASS — including `SimultaneousAwakeningsAllReachReady` (which asserts the partitioning still produces correct wake behavior).

- [ ] **Step 6: Commit (single atomic commit with test + fix)**

```bash
git add tests/test_engine.cpp src/scheduler/SchedulerEngine.cpp
git commit -m "fix(scheduler): read sleeper states off-lock in tickSleepingProcesses

Step 3 of the snapshot-tick-rebuild pattern called p->getState() while
holding stateMutex_, violating the discipline documented in the Track 1
design spec (line 422) and CLAUDE.md. Hoist the state reads above the
lock and use a pointer set as a pure-data predicate during the rebuild.

Regression test: EngineTest.TickSleepingDoesNotHoldStateMutexDuringRebuild
deadlocks against the old implementation, passes against the new one."
```

---

## Self-Review

**1. Spec coverage.** The fix targets exactly the rule cited in `docs/superpowers/specs/2026-05-26-track1-scheduler-design.md:422` and `CLAUDE.md` "Modularity rules". No other spec text constrains this code path. The new test mirrors the existing `TickSleepingDoesNotHoldStateMutex` test convention, so it integrates with the established regression suite.

**2. Placeholder scan.** No TBDs, no "implement later", every code block is complete. Commands are exact (`ctest --timeout 10`, explicit `git add` paths).

**3. Type consistency.** `getStateHook` matches `tickSleepHook`'s `std::function<void()>` shape. `unordered_set<IProcess*>` is the only new type — used locally, no header leakage. `getState()` signature unchanged from `IProcess` (`ProcessState ... const override`); only its definition site moves from header-inline to .cpp.

---

## Execution choice

Plan complete and saved to `docs/superpowers/plans/2026-06-22-fix-ticksleeping-rebuild-lock.md`. Two execution options:

1. **Subagent-Driven (recommended)** — dispatch a fresh subagent per task with review between tasks.
2. **Inline Execution** — execute tasks in this session with checkpoints.
