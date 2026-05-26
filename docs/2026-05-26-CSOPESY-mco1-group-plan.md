# MCO1 Group Work Plan — CSOPESY OS Emulator

_Generated: 2026-05-26_

---

## Project Overview

**What we're building:** A C++ command-line OS emulator that simulates real-time CPU process scheduling. The system will accept text commands (like a Linux/Windows shell), spawn dummy processes, run them on simulated CPU cores using FCFS or Round Robin scheduling, and let the user inspect process status at any time.

**Why it matters:** This is the major group output worth **35% of the final grade**. The assessment is a black-box, time-pressured quiz in Week 7 (mockup) and Week 8 (actual) — we record MP4 videos demonstrating our CLI against given test cases. **No recompilation allowed during the quiz** — only `config.txt` parameter changes.

**Stack:**
- C++17 currently in repo → **needs bump to C++20** (required for `std::semaphore` and modern threading)
- **CMake** build system (cross-platform — works on Mac, Linux, Windows)
- Standard library only (`<thread>`, `<mutex>`, `<condition_variable>`, `<atomic>`)
- No external dependencies required

**Deadlines:**
- **Week 7** — Mockup test case + quiz
- **Week 8** — Actual test case + quiz
- **Week 13** — Final submission (source code, PPT, videos)

**Today:** 2026-05-26 → roughly **2 weeks** until the mockup quiz.

---

## Current Repo State

**Repo:** [github.com/achibukz/csopesy-mco1](https://github.com/achibukz/csopesy-mco1)
**Local path:** `/Users/achibukz/Code/GitHub/csopesy-mco1`
**Last commit:** `5aa15cf add: main menu` (May 19)

### What's already in place

```
csopesy-mco1/
├── CMakeLists.txt          ← CMake config (C++17, needs bump to C++20)
├── README.md               ← build instructions stub
├── CLAUDE.md               ← project-specific notes (file layout plan)
├── MO1 - OS Emulator - Process Scheduler.docx  ← spec doc
├── .gitignore
└── src/
    └── main.cpp            ← entry point + stub command dispatcher
```

### What `main.cpp` currently does

- Prints the CSOPESY ASCII banner
- Has a `while` loop reading commands
- Recognizes (as stubs, all printing "command recognized. Doing something."): `initialize`, `exit`, `clear`, `screen`, `scheduler-start`, `scheduler-stop`, `report-util`
- No actual logic behind any command yet
- No `Console`, `Config`, `Process`, `Scheduler`, `CPU`, or `Instructions` files exist yet — only `main.cpp`

### Planned file layout (from existing repo CLAUDE.md)

```
src/
  main.cpp          — entry point, creates Console
  Console.h/.cpp    — main menu CLI, command dispatch
  Config.h/.cpp     — reads config.txt
  Process.h/.cpp    — Process state, instruction queue
  Instructions.h    — IInstruction base + all instruction types
  Scheduler.h/.cpp  — abstract IScheduler + FCFSScheduler + RRScheduler
  CPU.h/.cpp        — CPU core worker threads
config.txt          — runtime configuration
```

### What needs to happen first (before track work)

1. **Bump CMakeLists.txt to C++20** — `set(CMAKE_CXX_STANDARD 20)` instead of 17
2. **Refactor `main.cpp`** — move dispatch logic into a `Console` class; keep `main.cpp` as a thin entry point
3. **Create stub header files** for each planned module so all 4 people can include and compile against them in parallel
4. **Add `config.txt`** with sane default values at repo root

This is the "Day 1 scaffolding" step before tracks 1–4 begin in parallel.

---

## What We Need to Build (High-Level)

The emulator has 4 major subsystems that must work together as one immediate-mode loop:

1. **Console + Command Parser** — the main menu CLI that listens for user input, parses commands, and dispatches to the right subsystem. Must enforce the rule that `initialize` is called before anything else.

2. **Process & Instruction Engine** — defines what a "process" is in the emulator, what instructions it can run (PRINT, DECLARE, ADD, SUBTRACT, SLEEP, FOR), and how variables are stored. Each process has a randomized sequence of instructions generated when it's created.

3. **CPU Scheduler** — the heart of the system. Simulates multiple CPU cores using `std::thread`s, manages a ready queue, and runs processes using either FCFS or Round Robin (set via `config.txt`). Also handles auto-generation of dummy processes when `scheduler-start` is invoked.

4. **Screen Multiplexer + Reporting** — the "task manager" view. Implements the `screen -s`, `screen -r`, `screen -ls` commands (like Linux `screen`), the `process-smi` inside-screen command, and the `report-util` command that writes a snapshot to `csopesy-log.txt`.

All four subsystems share state (process queues, CPU utilization, process logs) and must be thread-safe — the scheduler runs in the background while the console keeps accepting input.

---

## Task Summary

| #   | Track                          | Owner      | Core Responsibility                                                                          |
| --- | ------------------------------ | ---------- | -------------------------------------------------------------------------------------------- |
| 1   | Scheduler Core + Threading     | Aki        | FCFS/RR algorithms, CPU thread pool, ready queue, tick counter, auto-process generator       |
| 2   | Process & Instruction Engine   | Job + Tony | `Process` class, all 6 instruction types, variable engine, randomized instruction generation |
| 3   | Console + Parser + Config      | Aki        | Main loop, command dispatcher, `config.txt` parser, initialization gate                      |
| 4   | Screen Multiplexer + Reporting | Mariel     | `screen` subcommands, `process-smi`, `report-util`, `csopesy-log.txt` output                 |
|     |                                |            |                                                                                              |

**Shared (everyone contributes):**
- Architecture sync on Day 1
- Integration testing
- Technical PPT (each person presents their module)
- Test case demo videos
- README.txt

> Detailed C++ class structures and interface contracts are in the companion document: `2026-05-26-CSOPESY-mco1-code-reference.md`

---

## Track 1 — Scheduler Core + Threading (Person A)

**Why this track is central:** Touches every other subsystem. This person needs strong grasp of threading, synchronization, and producer-consumer patterns.

### Deliverables

1. **CPU emulation** — one `std::thread` per simulated core. Each thread runs a worker loop that pulls a process from the ready queue and executes its instructions.

2. **Scheduler class (singleton)** — manages the ready queue, running list, finished list, and the global CPU tick counter. Exposes thread-safe getters so other tracks can snapshot state without race conditions.

3. **FCFS algorithm** — pull from front of queue, run process to completion (or until `SLEEP`), then move to next.

4. **Round Robin algorithm** — pull from front, run for `quantum-cycles` ticks. If unfinished, push to back of queue. Honor `SLEEP` (yields immediately, regardless of quantum).

5. **`scheduler-start` auto-generation** — a background thread that every `batch-process-freq` ticks calls Person B's generator and enqueues a new process. Names auto-increment: `p01, p02, ..., p1240`.

6. **`delays-per-exec` busy-wait** — between executing instructions, spin for N ticks while still holding the CPU (different from `SLEEP`, which relinquishes).

**Files owned:** `src/Scheduler.h/cpp` (incl. `IScheduler`, `FCFSScheduler`, `RRScheduler`), `src/CPU.h/cpp`

---

## Track 2 — Process & Instruction Engine (Person B)

**Why this track is self-contained:** Doesn't need to know how the scheduler works internally. Just needs to define what a process looks like and how each instruction behaves.

### Deliverables

1. **`Process` class** — holds PID, name, current instruction line, total instructions, list of `ICommand`s, variable map (`uint16_t`), print log, creation timestamp, and state (READY/RUNNING/SLEEPING/FINISHED).

2. **`ICommand` interface + 6 concrete implementations:**
   - `PRINT(msg)` — appends to process's printLog (NOT stdout). Default: `"Hello world from <process_name>!"`. Can concatenate one variable.
   - `DECLARE(var, value)` — initialize a uint16 variable.
   - `ADD(var1, op2, op3)` — `var1 = op2 + op3`. Operands can be vars or literals. Auto-declare missing vars as 0. Clamp result to `[0, 65535]`.
   - `SUBTRACT(var1, op2, op3)` — same shape as ADD.
   - `SLEEP(X)` — process sleeps for X ticks. **Relinquishes CPU.**
   - `FOR([instructions], repeats)` — nested loop. **Max nesting depth = 3.**

3. **Variable engine rules:**
   - Type: `uint16_t`, clamped to `[0, 65535]`
   - Auto-declare with value 0 if used before `DECLARE`
   - Variables persist for the process's full lifetime
   - Overflow on ADD/SUBTRACT clamps (does NOT wrap around)

4. **Instruction Generator** — given `min-ins` and `max-ins` from config, generate a randomized mix of the 6 instruction types. Respect the FOR depth ≤ 3 rule.

5. **Execution semantics** — `executeNext()` is called by Person A's `CPUCore`. Process advances one instruction, updates state, handles SLEEP yielding.

**Files owned:** `src/Process.h/cpp`, `src/Instructions.h` (incl. `IInstruction` base + all 6 instruction types + instruction generator)

---

## Track 3 — Console + Command Parser + Config (Person C)

**Why this track finishes first:** UI layer is independent — can be developed against stub scheduler/process classes. This person can move to integration testing earliest.

### Deliverables

1. **Main loop (immediate mode)** — `while (running) { refresh(); handleInput(); dispatch(); }`. This is the entry point.

2. **Top-level command dispatcher** — recognizes:
   - `initialize` — read `config.txt`, init scheduler with parameters
   - `exit` — set `running = false`
   - `scheduler-start` / `scheduler-stop` — delegate to Person A
   - `screen ...` — delegate to Person D
   - `report-util` — delegate to Person D
   - `clear` — wipe terminal

3. **Initialization gate** — before `initialize` is called, ONLY `initialize` and `exit` are recognized. Everything else prints: `"Please initialize first."`

4. **`config.txt` parser** — space-separated key-value pairs. Parameters:
   - `num-cpu` — number of cores [1, 128]
   - `scheduler` — `"fcfs"` or `"rr"`
   - `quantum-cycles` — RR time slice [1, 2³²]
   - `batch-process-freq` — process generation rate [1, 2³²]
   - `min-ins` / `max-ins` — instruction count range per process
   - `delays-per-exec` — busy-wait between instructions [0, 2³²]
   - Validate all ranges; reject invalid configs.

5. **Console banner / prompt** — ASCII-art "CSOPESY" header on startup, clear prompt indicator (e.g., `csopesy> `).

**Files owned:** `src/Console.h/cpp`, `src/Config.h/cpp`, `src/main.cpp` (refactor existing stub into Console class)

---

## Track 4 — Screen Multiplexer + Reporting (Person D)

**Why this track is decoupled:** Reads scheduler/process state via Person A's thread-safe getters but doesn't mutate them. Clean read-only consumer of the shared state.

### Deliverables

1. **`ScreenManager` class** — tracks all created processes by name, manages the "current screen" state (main menu vs. inside a process screen).

2. **`screen -s <name>`** — asks Person B to generate a process with random instructions, registers it with Person A's scheduler, clears the console, and "moves into" the process screen showing PID, name, current line, total lines, timestamp.

3. **`screen -r <name>`** — looks up process by name. If not found or already finished → `"Process <name> not found."` Otherwise, show same view as `-s`.

4. **`screen -ls`** — prints summary view:
   - CPU utilization %
   - Cores used / cores available
   - Running processes with timestamps and progress (`current_line / total_lines`)
   - Finished processes
   - **Design this output thoughtfully** — a clean layout makes debugging the scheduler much easier.

5. **`process-smi`** (inside a screen) — prints PID, name, state, and **all accumulated PRINT logs** from that process. If finished, append `"Finished!"`.

6. **`report-util`** — same data as `screen -ls`, but ALSO append to `csopesy-log.txt` with timestamp. Must include ALL processes (running AND finished — finished processes don't get dropped from tracking).

**Files owned:** `src/ScreenManager.h/cpp`, `src/Reporter.h/cpp` (new files, not yet in repo)

---

## Critical Integration Points

These are the boundaries where bugs hide — pay attention during integration:

- **Scheduler ↔ Process:** Person A's `CPUCore` calls `process->executeNext()`. The process must update its own state thread-safely; the scheduler must observe state changes via atomics or mutex-protected reads.
- **Console ↔ Scheduler:** `scheduler-start` and `scheduler-stop` are async — they signal Person A's background generator thread, they don't block.
- **Screen ↔ Scheduler:** Person D reads scheduler state (running/finished lists, CPU %) — Person A must return safe snapshots, not live references.
- **PRINT output flow:** Person B's `PrintCommand` writes to `process->printLog`. Person D's `process-smi` reads it. Never goes through stdout directly.
- **Config gate:** Person C's `initialized` flag must be checked before ANY command other than `initialize`/`exit` runs.

---

## Timeline

| Week | Dates | Milestone |
|------|-------|-----------|
| **W5 (now)** | May 26 – 31 | **Day 1: Architecture sync.** Lock interfaces, set up repo, scaffold files with stubs. Each person starts their track. |
| **W6** | Jun 1 – 7 | Tracks ~70% done. Persons A + B integrate (scheduler runs simple processes). Persons C + D integrate (console + screen show real data). |
| **W7** | Jun 8 – 14 | **Mockup quiz.** Full integration. Run sample `config.txt` files. Fix bugs found during mockup. |
| **W8** | Jun 15 – 21 | **Actual quiz.** Polish, record demo videos. |
| **W13** | ~Jul 21 | Final submission (source + PPT + videos). |

---

## Day 1 Kickoff Agenda

Suggested ~1 hour meeting:

1. **Walk through the existing repo** (10 min) — `github.com/achibukz/csopesy-mco1`; everyone clones it, runs `cmake . && make && ./mco1` to confirm it builds
2. **Confirm tooling** (10 min) — agree on bumping `CMakeLists.txt` to C++20; everyone has a working C++20 compiler (Clang 11+/GCC 10+/MSVC 2019+)
3. **Branch strategy** (10 min) — one branch per track (e.g., `track-scheduler`, `track-process`, `track-console`, `track-screen`), PRs into `main`
4. **Walk through interface contracts** (20 min) — see companion code reference doc; adjust signatures as a team
5. **Scaffold stub files** (5 min) — Person C creates empty `Console.h/cpp`, `Config.h/cpp`; Person A creates `Scheduler.h/cpp`, `CPU.h/cpp`; Person B creates `Process.h/cpp`, `Instructions.h`; Person D creates `ScreenManager.h/cpp`, `Reporter.h/cpp`. Commit empty headers so everyone can include them.
6. **Pick a sample `config.txt`** (10 min) — agree on a "hello world" test case everyone can build against
7. **Assign tracks + sync cadence** (5 min) — confirm Person A/B/C/D; recommend 2× weekly standups (e.g., Tue + Fri, 15 min)

---

## Submission Requirements (already known, plan around these)

| Artifact | Description |
|----------|-------------|
| **SOURCE** | Source code + `README.txt` (group members' names, run instructions, entry main file). Or GitHub link. |
| **PPT** | Technical report covering: command recognition, console UI, command interpreter, process representation, scheduler implementation |
| **Video (MP4)** | One per test case, demonstrating CLI output against the given inputs |

**Grading per test case:**
- CLI did not pass, no workaround → 0 points
- CLI did not pass, workaround exists → partial points
- CLI passed with varying inputs → full points

---

## Reference Files

- Wiki: `wiki/AY2526-T3/CSOPESY-Intro-to-Operating-Systems/notes/2026-05-11-mco1-spec.md`
- Wiki: `wiki/AY2526-T3/CSOPESY-Intro-to-Operating-Systems/topics/os-emulator-architecture.md`
- Wiki: `wiki/AY2526-T3/CSOPESY-Intro-to-Operating-Systems/notes/2026-05-21-os-emulator-overview-lecture.md`
- Raw spec: `raw/AY2526-T3/CSOPESY/MO1 - OS Emulator - Process Scheduler.docx`
- Companion: `output/2026-05-26-CSOPESY-mco1-code-reference.md` — C++ class skeletons and interface contracts
