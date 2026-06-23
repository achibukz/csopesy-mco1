# CSOPESY MCO1 — Code Review (against MO1 docx spec)

**Date:** 2026-06-24
**Sole spec source:** `docs/MO1 - OS Emulator - Process Scheduler.docx`
**Method:** every finding below quotes the docx. If the docx is silent on something, it is not listed here.

Severity:
- **H** — docx says it must work; code does not.
- **M** — docx says it must work; code partially works.
- **L** — docx says it must work; code works but a small detail diverges.

---

## Track 1 — Scheduler Core

### H0 — Generator drops batch-process-freq events
**File:** `src/scheduler/SchedulerEngine.cpp:90–121` (`generatorLoop`)
**Docx:** "Every X CPU ticks, a new process is generated and put into the ready queue for your CPU scheduler." And: "If one, a new process is generated at the end of each CPU cycle."
**Code:** `generatorLoop` waits on a 5 ms condition-variable timeout, then reads `current = tick_.load()`, sets `lastSeen = current`, and only spawns if `current % batchProcessFreq == 0`. Because `tickLoop` advances `tick_` every 1 ms, multiple tick boundaries can elapse between wake-ups. The generator only inspects the current tick value, so any boundaries it slept through are skipped — with `batch-process-freq 1` (your current config), most spawns are lost.
**Fix:** iterate every tick between `lastSeen + 1` and `current` and spawn once per matching boundary:
```cpp
uint64_t current = tick_.load();
for (uint64_t t = lastSeen + 1; t <= current; ++t) {
    if (generatorEnabled_.load() && factory_ && t % cfg_.batchProcessFreq == 0) {
        // spawn one process
    }
}
lastSeen = current;
```
The duplicated spawn path in `stepOnce()` (lines 60–77) needs the same correction.

Other Track 1 docx rules check out:
- "delays-per-exec ... busy-waiting scheme wherein the process remains in the CPU"
- "SLEEP(X) — sleeps the current process for X (uint8) CPU ticks and relinquishes the CPU"
- Config ranges: num-cpu [1, 128], quantum-cycles [1, 2^32], batch-process-freq [1, 2^32], min-ins [1, 2^32], max-ins [1, 2^32], delays-per-exec [0, 2^32]

---

## Track 2 — Process + Instructions

### M1 — FOR nesting capped at 2 instead of 3
**File:** `src/process/Instructions.cpp:90`
**Docx:** "For loops can be nested up to 3 times."
**Code:**
```cpp
const bool allowFor = depthRemaining > 1;
```
With `kMaxForDepth = 3`, this allows FOR at depths 3 and 2 only — maximum 2 nested FORs, not 3.
**Fix:** change `depthRemaining > 1` to `depthRemaining > 0`.

Everything else in Track 2 matches the docx:
- PRINT default `"Hello world from <process_name>!"` — `Instructions.cpp:19`
- PRINT with variable concat — `Instructions.cpp:22–24`
- DECLARE uint16 — `Instructions.cpp:27–29`
- ADD/SUBTRACT auto-declare missing vars as 0 — `Process.cpp:101`
- ADD clamp to max(uint16) — `Instructions.cpp:41`
- SUBTRACT floor at 0 — `Instructions.cpp:49`
- SLEEP uint8 ticks, relinquishes CPU — `Instructions.cpp:54–56`, `Process.cpp:114–118`
- Variables persist until process finishes — `Process.cpp` never erases from `variables_`
- Instructions pre-generated (not user-typed) — `InstructionGenerator::generate`

---

## Track 3 — Console + Config

### H1 — `clear` runs before initialize
**File:** `src/cli/Console.cpp:115–118`
**Docx:** "This must be called before any other command could be recognized, aside from 'exit'." and "No other commands should be recognized if the user hasn't typed this first."
**Code:**
```cpp
if (cmd == "clear") {
    clearScreen();
    return;
}

if (cmd != "initialize" && cmd != "exit" && !initialized_) {
    std::cout << "System not initialized. Run 'initialize' first.\n";
    return;
}
```
`clear` returns before the init gate, so it is recognized pre-initialize. Spec allows only `initialize` and `exit`.
**Fix:** move the `clear` branch below the init gate, or include `clear` in the gate's allow-list (then it will be rejected pre-init).

The docx does not specify the exact error string or prompt text, so those are not flagged.

---

## Track 4 — Screen Multiplexer + Reporting

### H2 — Track 4 entirely missing
**Files missing:** `src/ScreenManager.{h,cpp}`, `src/Reporter.{h,cpp}` (or equivalent)
**Current stubs:** `src/cli/Console.cpp:95–101` (`handleScreen`, `handleReportUtil` print placeholder text)

The docx requires the following, none of which exist:

| Feature | Docx requirement |
|--------|-------------------|
| `screen -s <name>` | "Create a new process via 'screen -s <process name>' command." Console clears and "moves" into the process screen. |
| `screen -ls` | "Lists all running processes via 'screen -ls' command." Must show: CPU utilization, cores used, cores available, running summary, finished summary. |
| `screen -r <name>` | Re-attach by name. If not found or finished, print exactly `"Process <process name> not found."` |
| `process-smi` | Inside a screen: prints process info + accompanying print-instruction logs. If finished, print `"Finished!"` after process name, ID, and logs. |
| `exit` (inside screen) | Returns to main menu. |
| `report-util` | "Generates CPU utilization report." Saves the same content as `screen -ls` to `csopesy-log.txt`. |
| Finished-process retention | "all finished and currently running processes must be reported in the 'report-util' command" — finished processes must remain accessible to `report-util`. Engine already keeps `finishedProcs_`, so this is just wiring. |

**Required literal strings (per docx):**
- `"Hello world from <process_name>!"` — already in `Instructions.cpp:19` ✓
- `"Process <process name> not found."` — for failed `screen -r`
- `"Finished!"` — appended to `process-smi` output for finished processes

---

## Summary

| Track | Findings |
|-------|----------|
| 1 | H0 |
| 2 | M1 |
| 3 | H1 |
| 4 | H2 (whole track) |

**Order to fix:**
1. H1 — one-line fix in `Console.cpp`.
2. M1 — one-character fix in `Instructions.cpp:90`.
3. H0 — catch-up loop in `SchedulerEngine.cpp` generator (and matching path in `stepOnce`).
4. H2 — implement Track 4 (`ScreenManager`, `Reporter`, and wire them into `Console`).
