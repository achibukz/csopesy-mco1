# Prof Consultation Notes — 2026-06-16

Source: Mariel relaying live consultation today.

## 1. uint16 over/underflow on ADD / SUBTRACT
- Allowed to either clamp at a base value, OR show the over/underflow.
- Whichever is easier to implement.
- Status in repo: Track 2 (`Process` / `Instructions`) not implemented yet — decide policy when writing it. No action now.

## 2. Process generator
- Put a **limit** in the generator so it won't go beyond max levels.
- Dummy processes must be **dynamically generated** (not pre-seeded).
- Processes will spawn **continuously** as the OS runs.
- Status in repo: `SchedulerEngine::generatorLoop()` (`src/scheduler/SchedulerEngine.cpp:89-120`) already spawns continuously and dynamically via `factory_`. Missing: upper bound on `nextPid_`. Plan: hardcoded cap (e.g. `kMaxProcesses = 1000`) inside `SchedulerEngine`, skip generation when reached. No new `config.txt` key.

## 3. SLEEP
- Can be a **separate function** outside of the instructions/commands path.
- Consequence: it "won't see the number of ticks" — so the engine, not the instruction stream, must own tick accounting.
- Status in repo: already aligned. `SchedulerEngine::adoptSleeping`, `moveToSleeping`, `tickSleepingProcesses` (`src/scheduler/SchedulerEngine.cpp:174-223`) handle the sleep queue at the engine level. `IProcess::tickSleep()` is the per-process hook the engine calls once per global tick.
- Track 2 contract to remember: `Process::tickSleep()` must decrement its own remaining-ticks counter and flip `ProcessState` back to `READY` at zero.

## 4. Main console
- Can have **fixed dimensions** — does not need to adapt to terminal size.
- If everything doesn't fit on screen, the user adjusts their terminal, not us.
- Status in repo: already fixed-width (`src/cli/Console.cpp`, `src/cli/Demo.cpp` use static `setw()` widths and fixed ASCII header). No action.

## 5. Unit testing
- Allowed to use external libraries.
- Status in repo: GoogleTest v1.14.0 via `FetchContent` (`CMakeLists.txt:37-61`). No action.

## Cross-check with schoolMem/csopesy notes
Wiki pages match what we built (no contradictions):
- `CSOPESY - Additional Details on CPU Scheduling Mechanisms.txt` — PCB + ready/running/sleeping/finished partition.
- `CSOPESY - CPU Scheduling + FCFS.txt`, `CSOPESY - Multicore Scheduling.txt` — FCFS + per-core worker model.
- `CSOPESY - C++ Multithreading.txt` — `std::thread` / `std::mutex` / `condition_variable` patterns used in `SchedulerEngine.cpp`.
