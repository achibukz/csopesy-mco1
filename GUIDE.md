# CSOPESY MCO1 — Repo Guide

A C++20 CLI emulator that simulates an OS process scheduler with a `screen`-style multiplexer. Implements **FCFS** and **Round Robin** across configurable CPU cores.

---

## 1. Requirements

- **CMake** ≥ 3.14
- **C++20** compiler (Clang, GCC, or MSVC)
- **pthreads** (auto-detected on macOS/Linux)
- Internet on first build (CMake fetches GoogleTest v1.14.0 via `FetchContent`)

No `pip`, no `npm`, no extra dependencies.

---

## 2. Build

From the repo root:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
```

This produces three binaries in `build/`:

| Binary             | Purpose                                                            |
|--------------------|--------------------------------------------------------------------|
| `mco1`             | The interactive CLI emulator — main deliverable                    |
| `scheduler_demo`   | Standalone scheduling visualizer (FCFS then RR, fixed scenario)    |
| `mco1_tests`       | GoogleTest unit + integration suite                                |

---

## 3. Run

### Main emulator
```bash
./build/mco1
```
Opens the REPL at the main menu. First command **must** be `initialize`.

### Visual demo (no config needed)
```bash
./build/scheduler_demo
```

### Tests
```bash
ctest --test-dir build --output-on-failure
# or directly:
./build/mco1_tests
```

---

## 4. Configuration — `config.txt`

`initialize` reads `config.txt` from the working directory. All seven keys are required.

| Key                  | Type   | Range      | Meaning                                                    |
|----------------------|--------|------------|------------------------------------------------------------|
| `num-cpu`            | int    | [1, 128]   | Number of simulated CPU cores (one `std::thread` each)    |
| `scheduler`          | string | `fcfs`/`rr`| Scheduling algorithm                                       |
| `quantum-cycles`     | int    | [1, ∞)     | RR time slice (ignored under FCFS)                        |
| `batch-process-freq` | int    | [1, ∞)     | Spawn a new process every N ticks during `scheduler-start`|
| `min-ins`            | int    | [1, ∞)     | Min instructions per generated process                    |
| `max-ins`            | int    | [1, ∞)     | Max instructions per generated process                    |
| `delays-per-exec`    | int    | [0, ∞)     | Busy-wait ticks between instructions (stays on CPU)       |

Current default (`config.txt`):
```
num-cpu 4
scheduler rr
quantum-cycles 5
batch-process-freq 1
min-ins 1000
max-ins 2000
delays-per-exec 0
```

---

## 5. CLI Commands

### Main menu
| Command                | What it does                                                            |
|------------------------|-------------------------------------------------------------------------|
| `initialize`           | Loads `config.txt`. **No other command works until this succeeds.**     |
| `screen -s <name>`     | Create + attach a new process screen                                    |
| `screen -r <name>`     | Re-attach to an existing (non-finished) process                         |
| `screen -ls`           | List all processes and CPU utilization                                  |
| `scheduler-start`      | Begin batch process generation                                          |
| `scheduler-stop`       | Stop batch generation                                                   |
| `report-util`          | Print utilization to stdout **and** write `csopesy-log.txt`             |
| `demo`                 | Run the in-REPL scheduling visualizer (resets scheduler state)          |
| `clear`                | Wipe terminal                                                           |
| `exit`                 | Clean shutdown                                                          |

### Inside a process screen
| Command       | What it does                              |
|---------------|-------------------------------------------|
| `process-smi` | Show process info + instruction logs      |
| `exit`        | Return to main menu                       |

### Auto-generated process instructions (not typed by user)
`PRINT`, `DECLARE`, `ADD`, `SUBTRACT`, `SLEEP(X)`, `FOR([...], n)` (nested up to 3 levels). All uint16 math clamps at `[0, 65535]`.

---

## 6. Typical Session

```text
$ ./build/mco1
> initialize
> scheduler-start
> screen -ls            # see workers picking up generated p01, p02, ...
> screen -r p03         # peek at one
process-smi
exit                    # back to main menu
> scheduler-stop
> report-util           # writes csopesy-log.txt
> exit
```

---

## 7. Repository Layout

```
include/                 # public headers, grouped by module
  scheduler/             # IProcess, SchedulingPolicy, SchedulerEngine, CPU, Scheduler
  config/                # Config
  cli/                   # Console, Demo
src/
  main.cpp               # entry point
  scheduler/             # engine, policies, CPU threads, public facade
  config/                # config.txt parser + validation
  cli/                   # REPL + demo runner
demos/
  scheduler_demo.cpp     # standalone visualizer
tests/
  MockProcess.{h,cpp}    # fake IProcess for tests
  test_config.cpp        # parser + validation
  test_policy.cpp        # FCFS + RR pure logic
  test_engine.cpp        # engine driven by stepOnce, no real threads
  test_scheduler.cpp     # full threaded integration
  test_console.cpp       # REPL dispatcher
  fixtures/              # valid + invalid config.txt samples
docs/
  2026-05-26-CSOPESY-mco1-group-plan.md          # authoritative spec
  2026-05-26-CSOPESY-mco1-code-reference.md      # authoritative spec
  superpowers/specs/2026-05-26-track1-scheduler-design.md
  2026-05-28-code-review-tracks-1-3.md           # latest code review
config.txt               # runtime config (do NOT hardcode in source)
CLAUDE.md                # project rules for AI assistants
```

---

## 8. Architecture Flow

```
                +----------------------+
   user input → |   Console (REPL)     |  src/cli/Console.cpp
                +----------+-----------+
                           |
                           v
                +----------------------+
                |   Scheduler (facade) |  src/scheduler/Scheduler.cpp
                +----------+-----------+
                           |
                           v
                +----------------------+
                |  SchedulerEngine     |  owns ready/running/sleeping/finished queues
                |  (single mutex)      |  + tick counter + process generator
                +----+----------+------+
                     |          |
       +-------------+          +---------------+
       v                                        v
+-------------+                         +-------------------+
| CPU thread  | × num-cpu               | ISchedulingPolicy |
| pulls work  | <---- policy decides -- | (FCFS or RR)      |
+-------------+                         +-------------------+
       |
       v
+-------------+
| IProcess    |  Track 2 hook — tests use MockProcess
+-------------+
```

Key invariants (enforced by `CLAUDE.md` modularity rules):

- `Scheduler` depends on `IProcess` only — never on a concrete `Process`.
- All scheduling decisions live in `ISchedulingPolicy` subclasses. No algorithm logic in `Scheduler`, `CPU`, or `SchedulerEngine`.
- `SchedulerEngine` owns all shared mutable state; CPUs and policies touch it only through engine methods.
- Threading lives **only** in `CPU.cpp` and `SchedulerEngine.cpp`. `SchedulingPolicy.cpp`, `Config.cpp`, `Scheduler.cpp` compile without `<thread>`.
- Public snapshot methods return values, never references.
- Sleeping processes park in `SchedulerEngine::sleepingProcs_` (not pinned to a core).
- A "tick" is an integer counter, **not** wall-clock time.
- `delays-per-exec` busy-waits on CPU; `SLEEP(X)` relinquishes the CPU.

---

## 9. Testing

```bash
ctest --test-dir build --output-on-failure
```

Test layout mirrors the module split:

| File                 | Scope                                                  |
|----------------------|--------------------------------------------------------|
| `test_config.cpp`    | Parser, range validation, malformed input              |
| `test_policy.cpp`    | FCFS + RR decisions in isolation                       |
| `test_engine.cpp`    | `stepOnce` engine, deterministic, no real threads      |
| `test_scheduler.cpp` | Full threaded integration via `Scheduler`              |
| `test_console.cpp`   | REPL dispatcher (no interactive stdin)                 |

`MockProcess` substitutes for Track 2's not-yet-implemented `Process` and exposes a `tickSleepHook` used to assert the engine doesn't hold the state mutex while ticking sleepers.

---

## 10. Common Gotchas

- **First command must be `initialize`.** Everything else (except `exit`) is rejected before that.
- **Finished processes are not re-attachable** via `screen -r`.
- **`screen -ls` and `report-util` print identical data** — only `report-util` also writes `csopesy-log.txt`.
- **`min-ins` and `max-ins` start at 1**, not 0 — do not test below spec.
- Do **not** hardcode config values; always go through `config.txt`.
- Co-author trailers (`Co-Authored-By: Claude ...`) must never appear in commits.

---

## 11. Where to Read Next

- Authoritative spec: `docs/2026-05-26-CSOPESY-mco1-group-plan.md`
- Reference behavior: `docs/2026-05-26-CSOPESY-mco1-code-reference.md`
- Track 1 + 3 design: `docs/superpowers/specs/2026-05-26-track1-scheduler-design.md`
- Latest code review: `docs/2026-05-28-code-review-tracks-1-3.md`
- Project rules (for humans and AI): `CLAUDE.md`
