# Code Review — `feat/scheduling` branch (Tracks 1 + 3)

_Reviewed: 2026-05-28_
_Reviewer: Opus 4.7_
_Scope: whole repo, audited against the authoritative specs._

Authoritative references used:
- `docs/2026-05-26-CSOPESY-mco1-group-plan.md` (GP)
- `docs/2026-05-26-CSOPESY-mco1-code-reference.md` (CR)
- `docs/superpowers/specs/2026-05-26-track1-scheduler-design.md` (DS)
- `CLAUDE.md` (CM)

Tracks 2 (Process/Instructions) and 4 (ScreenManager/Reporter) are intentional stubs and not in scope.

---

## HIGH — functional spec deviations

### H1. Generator thread skips `batch-process-freq` events in real-threaded mode
**File:** `src/scheduler/SchedulerEngine.cpp:89-119`

```cpp
uint64_t current = tick_.load();
if (current == lastSeen) continue;
lastSeen = current;
if (current % cfg_.batchProcessFreq != 0) continue;
```

The generator wakes via `tickCv_.wait_for(..., 5 ms)` (line 94). The tick thread advances every 1 ms (line 81). Between two wake-ups, the tick can advance 4–10 counts. The loop snapshots only the *latest* `tick_` and modulo-checks it once. If `batchProcessFreq=2` and the generator wakes at tick=4 then tick=10, multiples 6 and 8 are dropped — production process generation runs at roughly 1/5 the rate the spec promises (GP:131, DS:35: *"every `batch-process-freq` ticks"*).

`stepOnce()` works correctly because it fires the generator inline per tick (line 60), which is why `GeneratorFiresEveryBatchProcessFreqTicks` passes. The threaded test `GeneratorProducesProcesses` only asserts `created > 0`, not the rate, so the bug is uncovered.

**Fix:** iterate from `lastSeen+1` through `current` and fire once for each `t % freq == 0`, or replace the CV wait with a per-tick notification token.

---

### H2. `clear` bypasses the initialization gate
**File:** `src/cli/Console.cpp:113-116`

GP:185-186 and CR:380-401: *"before `initialize` is called, ONLY `initialize` and `exit` are recognized. Everything else prints: 'Please initialize first.'"*

Current dispatch:
```cpp
if (cmd == "clear") { clearScreen(); return; }   // runs unconditionally
if (cmd != "initialize" && cmd != "exit" && !initialized_) { ...gate... }
```

`clear` clears the screen pre-`initialize`. This violates the gate. Either move the gate above the `clear` shortcut, or update CLAUDE.md to call out the exemption explicitly.

---

## MEDIUM — UX / wording / hygiene

### M1. Init-failure message diverges from spec
**File:** `src/cli/Console.cpp:119`

- Spec: `"Please initialize first.\n"` (GP:186, CR:391).
- Actual: `"System not initialized. Run 'initialize' first.\n"`.

Quiz grading is black-box on output strings — match the spec verbatim.

### M2. EOF on stdin exits without shutdown
**File:** `src/cli/Console.cpp:138`

`if (!std::getline(std::cin, line)) break;` skips `handleExit()`. The scheduler ends up cleaned by the singleton's `engine_`/`cores_` destructors at program exit, which is correct but inconsistent with the `exit` command path. Call `handleExit()` (or its body) on the EOF branch.

### M3. Prompt text
**File:** `src/cli/Console.cpp:137`

- Spec prompt (CR:196): `"csopesy> "`.
- Actual: `"Enter a command: "`.

### M4. `toSchedulerConfig(cfg)` called twice in `handleInitialize`
**File:** `src/cli/Console.cpp:60, 63`

Trivial dup; cache it once.

### M5. Per-CPU `coreId` is not retained in the engine
**File:** `src/scheduler/SchedulerEngine.cpp:133` (`int /*coreId*/`)

Track 4 `screen -ls` (GP:218; CR:471-473) requires a `Core: <n>` column per running process. Today `markRunning` discards the coreId. Not in scope for Track 1 to print, but the data needs to be retained for Track 4 — flag now so Mariel doesn't refactor `markRunning`'s signature later.

---

## LOW — defensive / style

### L1. Generator thread always spawned even when disabled
**File:** `src/scheduler/SchedulerEngine.cpp:32-33`

Wakes every 5 ms to do nothing until `startGenerator()` flips the flag. Harmless, but a single CV for both "tick advanced" and "generator enabled" would be cleaner.

### L2. Duplicate name-padding logic in demo
**File:** `src/cli/Demo.cpp:71`

Reimplements `makeProcessName` inline. Reuse `SchedulerEngine`'s version.

### L3. Fixture coverage gap
**File:** `tests/fixtures/config_bad_range.txt`

Only exercises `num-cpu 0`. No fixture for `num-cpu 129` (upper bound). `BootsWithMaxNumCpu` covers 128 succeeding; add a `RejectsAboveMaxNumCpu` for symmetry.

### L4. `dispatch("exit")` is a silent no-op
**File:** `src/cli/Console.cpp:129`

Tests rely on this (`tests/test_console.cpp:18`), but anyone reading `dispatch` in isolation would expect symmetric behavior. Add a 1-line comment or set a flag.

### L5. `RRPolicy::shouldKeepRunning` with `quantum == 0` returns `true`
**File:** `src/scheduler/SchedulingPolicy.cpp:38`

Config rejects `quantum < 1` (`Config.cpp:60-63`), so unreachable in practice. The `if (quantum == 0)` guard is defensive; add a one-line comment so a future reader doesn't think RR can degenerate to FCFS.

---

## What passes cleanly

- **Locking discipline** — `tickSleepingProcesses()` snapshots under lock, ticks outside the lock, re-acquires (DS:417). Previous deadlock from `docs/2026-05-26-scheduling-code-review-report.md` is fixed and pinned by `EngineTest.TickSleepingDoesNotHoldStateMutex`.
- **Type widths for delay counter** — `CPU::delayTicksRemaining_` is `uint64_t`. Earlier `int`-cast underflow risk is gone, covered by `LargeDelaysPerExecDoesNotUnderflow`.
- **Modularity rules in CLAUDE.md** — `SchedulingPolicy.cpp`, `Config.cpp`, `Scheduler.cpp` compile without `<thread>`. `SchedulerEngine` owns all queues; CPUs go through engine methods.
- **Snapshot-by-value** — `snapshotRunning`/`snapshotFinished` copy under lock.
- **Test suite** — 32 tests cover FCFS/RR semantics, quantum boundaries, SLEEP relinquishment, simultaneous wake, generator firing, CRLF configs, init-gate behavior, clean shutdown under 500 ms, concurrent snapshots, 128-core boot.
- **Build hygiene** — `-Wall -Wextra -Wpedantic`, C++20, GoogleTest pinned via FetchContent to v1.14.0.

---

## Priority for the mockup quiz

1. **H2** — init-gate strictness on `clear`. 1-line move.
2. **M1** — spec-verbatim init-failure string. String fix.
3. **M3** — prompt text. String fix.
4. **H1** — generator tick-miss. Only matters once Track 2 wires a factory; file an issue now.
5. **M5** — retain `coreId` in engine. Coordinate with Mariel before Track 4 starts.

Everything else is polish.
