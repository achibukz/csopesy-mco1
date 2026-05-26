# Track 1 + Track 3 — Scheduler Core, Threading, Config & Console Wiring — Design Spec

_Generated: 2026-05-26_
_Branch: feat/scheduling_
_Owner: Aki (Person A + Person C — both tracks)_

## Purpose

Implement the scheduler core, CPU threading, `config.txt` parser, and the Console wiring that drives them. Track 1 (scheduler) consumes config values that Track 3 parses, so both are delivered together to avoid a stub seam between them.

Deliver a modular, thread-safe scheduler that:
- reads its parameters from `config.txt`,
- gets driven by `initialize`, `scheduler-start`, `scheduler-stop`, and `exit` from the Console,
- remains independently buildable and testable before Tracks 2 and 4 land.

## Non-goals

- Not implementing `Process` internals (variables, print log, instruction list) — those belong to Track 2. Track 1 depends on a minimal `IProcess` interface only.
- Not implementing `screen` subcommands or `report-util` — those belong to Track 4. The Console leaves their handlers as the current stubs.
- The Console banner / clear / "please initialize first" gate already exist and stay as-is.

## Requirements traced to the spec

| Spec requirement | Where it lives in this design |
|------------------|-------------------------------|
| `initialize` reads `config.txt` and inits the scheduler | `Console::handleInitialize` → `parseConfig` → `Scheduler::initialize` |
| `initialize` gate: only `initialize`/`exit` work before init | already in `Console::dispatch`, preserved |
| `exit` shuts down cleanly | `Console::handleExit` → `Scheduler::shutdown` |
| `scheduler-start` / `scheduler-stop` | `Console::handleSchedulerStart/Stop` → `Scheduler::start/stopGenerator` |
| `config.txt` key-value parser with range validation | `src/Config.cpp::parseConfig` + `Config::validate` |
| One `std::thread` per simulated CPU core | `CPU::run()` worker loop |
| FCFS algorithm | `FCFSPolicy` |
| Round Robin algorithm with `quantum-cycles` | `RRPolicy` |
| `scheduler-start` batch generation at every `batch-process-freq` ticks | `SchedulerEngine::generatorLoop()` |
| `delays-per-exec` busy-wait between instructions (still holds CPU) | `CPU::run()` step 4 (`delayTicksRemaining_`) |
| `SLEEP` relinquishes CPU | `CPU::run()` step 3 + `IProcess::tickSleep()` |
| CPU tick is an integer counter (not wall clock) | `SchedulerEngine::tick_` (atomic uint64_t) |
| Thread-safe snapshots for screen views | `SchedulerEngine::snapshotRunning()` / `snapshotFinished()` |
| Process names auto-increment `p01..pNNNN` | `SchedulerEngine::generatorLoop()` uses `nextPid_` + zero-padded format |

---

## Architecture

```
                           ┌──────────────────────────────┐
                           │      Scheduler (facade)      │
                           │  singleton; public API for   │
                           │      Tracks 3 & 4            │
                           └──────────────┬───────────────┘
                                          │ owns
              ┌───────────────────────────┼───────────────────────────┐
              ▼                           ▼                           ▼
   ┌────────────────────┐    ┌────────────────────────┐    ┌────────────────────┐
   │ ISchedulingPolicy  │    │   SchedulerEngine      │    │   CPU x N cores    │
   │  - FCFSPolicy      │◄───┤  tick driver,          │◄───┤  one thread each   │
   │  - RRPolicy        │    │  generator, queues,    │    │  executes 1 instr  │
   │  (pure logic)      │    │  thread-safe state     │    │  per tick          │
   └────────────────────┘    └────────────────────────┘    └────────────────────┘
                                          ▲
                                          │ depends on
                                          ▼
                              ┌────────────────────────┐
                              │   IProcess (interface) │
                              │   Track 2 implements   │
                              └────────────────────────┘
```

**Design principles**

1. **Single tick source.** Only `SchedulerEngine::tickThread_` writes `tick_`. Every other component reads it. Ticks are logical (uint64_t counter), advanced at a fixed sleep interval in production and via `stepOnce()` in tests.
2. **Strategy pattern for scheduling policy.** The Scheduler does not know FCFS from RR. It defers to an injected `ISchedulingPolicy`. Adding a new algorithm = one new policy class.
3. **No `IProcess` internals in Track 1.** The scheduler reads state, calls `executeNext()`, calls `tickSleep()`. That's it. Variables, print log, instruction list are opaque.
4. **Snapshots only.** Public getters return copies (`std::vector<IProcess*>` by value). No mutable references escape the engine.

---

## Module breakdown

### `src/IProcess.h` — the seam

```cpp
#pragma once
#include <cstdint>
#include <string>

enum class ProcessState { READY, RUNNING, SLEEPING, FINISHED };

class IProcess {
public:
    virtual ~IProcess() = default;

    virtual int           getPID() const         = 0;
    virtual std::string   getName() const        = 0;
    virtual ProcessState  getState() const       = 0;
    virtual int           getCurrentLine() const = 0;
    virtual int           getTotalLines() const  = 0;
    virtual bool          isFinished() const     = 0;

    // Called by CPU once per tick while this process is on a core.
    virtual void executeNext(uint64_t currentTick) = 0;

    // Called by CPU once per tick while this process is SLEEPING.
    // When sleep elapses, implementation must transition state to READY.
    virtual void tickSleep() = 0;
};
```

Track 2's concrete `Process` will inherit from `IProcess`. The header lives under `src/` so all tracks include it from the same path.

### `src/SchedulingPolicy.h/.cpp` — strategy

```cpp
class ISchedulingPolicy {
public:
    virtual ~ISchedulingPolicy() = default;
    virtual IProcess* pickNext(std::queue<IProcess*>& ready) = 0;
    virtual bool shouldKeepRunning(IProcess* p, int ticksOnCore, uint32_t quantum) = 0;
    virtual void onPreempt(IProcess* p, std::queue<IProcess*>& ready) = 0;
};

class FCFSPolicy : public ISchedulingPolicy { /* run-to-completion */ };
class RRPolicy   : public ISchedulingPolicy { /* preempt at quantum */ };
```

- `FCFSPolicy::shouldKeepRunning` returns `true` unless `p->isFinished()` or `p->getState() == SLEEPING`.
- `RRPolicy::shouldKeepRunning` returns `false` when `ticksOnCore >= quantum`.
- `RRPolicy::onPreempt` pushes `p` to the back of the ready queue.
- `FCFSPolicy::onPreempt` is a no-op (FCFS never preempts).

**No threading code in this file.** Pure logic, trivially unit-testable.

### `src/CPU.h/.cpp` — one worker thread per core

```cpp
class CPU {
public:
    CPU(int coreId, SchedulerEngine& engine, ISchedulingPolicy& policy, uint32_t quantum, uint32_t delaysPerExec);
    void start();
    void stop();
    bool isBusy() const;
    IProcess* getCurrentProcess() const;
    int  getCoreId() const;

private:
    void run();   // tick-driven worker loop

    int                   coreId_;
    SchedulerEngine&      engine_;
    ISchedulingPolicy&    policy_;
    uint32_t              quantum_;
    uint32_t              delaysPerExec_;
    std::atomic<IProcess*> current_{nullptr};
    std::atomic<bool>     running_{false};
    std::thread           thread_;
    int                   ticksOnCurrent_{0};
    int                   delayTicksRemaining_{0};
    uint64_t              lastSeenTick_{0};
};
```

**Per-tick logic in `run()` (loops until `running_ == false`):**

```
wait for next tick from engine (CV)
if current_ == nullptr:
    current_ = engine.popReady()   // policy-driven
    if current_ != nullptr:
        engine.markRunning(current_, coreId_)
        ticksOnCurrent_ = 0
        delayTicksRemaining_ = 0
    else:
        continue                    // core idle this tick

if current_->getState() == SLEEPING:
    current_->tickSleep()
    if current_->getState() == READY:
        engine.clearRunning(current_)
        engine.enqueueReady(current_)
        current_ = nullptr
    ticksOnCurrent_++
    continue

if delayTicksRemaining_ > 0:
    delayTicksRemaining_--
    ticksOnCurrent_++
    continue

current_->executeNext(engine.currentTick())
delayTicksRemaining_ = delaysPerExec_
ticksOnCurrent_++

if current_->isFinished():
    engine.markFinished(current_)
    current_ = nullptr
    continue

if not policy_.shouldKeepRunning(current_, ticksOnCurrent_, quantum_):
    engine.preempt(current_, policy_)
    current_ = nullptr
```

`engine.preempt(p, policy)` is a single engine method that acquires `stateMutex_`, removes `p` from `runningProcs_`, decrements `coresUsed_`, calls `policy.onPreempt(p, ready_)`, then releases. This keeps the policy callback inside the lock without exposing the ready queue externally.

### `src/SchedulerEngine.h/.cpp` — tick, queues, generator

```cpp
class SchedulerEngine {
public:
    using ProcessFactory = std::function<std::unique_ptr<IProcess>(const std::string& name, int pid)>;

    SchedulerEngine(const SchedulerConfig& cfg, ISchedulingPolicy& policy);
    ~SchedulerEngine();

    void start();
    void stop();
    void stepOnce();   // advance one tick — used by tests

    void enqueueReady(IProcess* p);
    IProcess* popReady();                            // calls policy.pickNext() under lock
    void markRunning(IProcess* p, int coreId);       // adds to runningProcs_, ++coresUsed_
    void clearRunning(IProcess* p);                  // removes from runningProcs_, --coresUsed_
    void markFinished(IProcess* p);                  // clearRunning then push to finishedProcs_
    void preempt(IProcess* p, ISchedulingPolicy& policy);  // clearRunning + policy.onPreempt under lock

    uint64_t currentTick() const;
    void waitForNextTick(uint64_t& lastSeen);

    void setProcessFactory(ProcessFactory f);
    void startGenerator();
    void stopGenerator();

    std::vector<IProcess*> snapshotRunning() const;
    std::vector<IProcess*> snapshotFinished() const;
    int    coresTotal() const;
    int    coresUsed() const;
    double cpuUtilization() const;

private:
    void tickLoop();
    void generatorLoop();
    SchedulerConfig   cfg_;
    ISchedulingPolicy& policy_;
    std::atomic<uint64_t> tick_{0};
    std::atomic<bool>     running_{false};
    std::atomic<bool>     generatorRunning_{false};
    mutable std::mutex    tickMutex_;
    std::condition_variable tickCv_;

    std::queue<IProcess*>                ready_;
    std::vector<IProcess*>               runningProcs_;
    std::vector<IProcess*>               finishedProcs_;
    std::vector<std::unique_ptr<IProcess>> owned_;   // owns generator-spawned procs
    mutable std::mutex                   stateMutex_;

    std::thread       tickThread_;
    std::thread       genThread_;
    ProcessFactory    factory_;
    int               nextPid_{1};
    int               coresUsed_{0};
};
```

Two internal threads:
- **Tick thread** (`tickLoop`): increments `tick_`, broadcasts `tickCv_`. Sleeps 1 ms between ticks (constant in production; tests use `stepOnce()` instead and never start this thread).
- **Generator thread** (`generatorLoop`): woken every tick; when `tick_ % cfg_.batchProcessFreq == 0` calls `factory_(name, pid)` and enqueues it. Names are zero-padded: `p01`, `p02`, …, `p99`, `p100`, etc. (minimum width 2, grows as needed).

`stepOnce()` is the test seam: callable when `running_ == false`, it manually increments `tick_`, broadcasts the CV (so any test-owned CPUs receive it), and returns. No sleep, no separate tick thread.

### `src/Scheduler.h/.cpp` — public facade

```cpp
struct SchedulerConfig {
    int      numCpu;
    enum class Algo { FCFS, RR } algo;
    uint32_t quantum;
    uint32_t delaysPerExec;
    uint32_t batchProcessFreq;
};

class Scheduler {
public:
    static Scheduler& instance();

    void initialize(const SchedulerConfig& cfg);
    void shutdown();

    void enqueue(IProcess* p);
    void setProcessFactory(SchedulerEngine::ProcessFactory f);

    void startGenerator();
    void stopGenerator();

    double                 getCpuUtilization() const;
    int                    getCoresUsed() const;
    int                    getCoresTotal() const;
    std::vector<IProcess*> getRunningSnapshot() const;
    std::vector<IProcess*> getFinishedSnapshot() const;
    uint64_t               getCurrentTick() const;

    SchedulerEngine& engineForTesting();   // tests only

private:
    Scheduler() = default;
    Scheduler(const Scheduler&) = delete;
    Scheduler& operator=(const Scheduler&) = delete;

    std::unique_ptr<ISchedulingPolicy> policy_;
    std::unique_ptr<SchedulerEngine>   engine_;
    std::vector<std::unique_ptr<CPU>>  cores_;
    bool initialized_{false};
};
```

### `src/Config.h/.cpp` — config.txt parser (Track 3)

`Config` is the *full* parsed file. Track 1 needs a subset; Track 2 needs another subset. Keeping the full struct in one place avoids parallel definitions.

```cpp
// src/Config.h
struct Config {
    int      numCpu;            // [1, 128]
    enum class Algo { FCFS, RR } scheduler;
    uint32_t quantumCycles;     // [1, 2^32-1]
    uint32_t batchProcessFreq;  // [1, 2^32-1]
    uint32_t minIns;            // [1, 2^32-1]
    uint32_t maxIns;            // [minIns, 2^32-1]
    uint32_t delaysPerExec;     // [0, 2^32-1]

    bool validate(std::string& errOut) const;   // returns false + reason on bad ranges
};

// Throws std::runtime_error with a human-readable message on parse / validation failure.
Config parseConfig(const std::string& path);

// Track 1 reads only the subset it cares about.
SchedulerConfig toSchedulerConfig(const Config& full);
```

**File format** (one key-value pair per line, whitespace-separated):

```
num-cpu 4
scheduler rr
quantum-cycles 5
batch-process-freq 1
min-ins 1000
max-ins 2000
delays-per-exec 0
```

**Parser rules**
- Unknown keys → reject with `std::runtime_error`.
- Missing required keys → reject (all seven keys are required).
- Duplicate keys → last-wins, with a warning to stderr.
- `scheduler` must be exactly `"fcfs"` or `"rr"` (case-sensitive). Anything else rejects.
- `min-ins > max-ins` rejects.
- `num-cpu` outside [1, 128] rejects.
- All `uint32_t` fields: numeric, non-negative, fit in `uint32_t`.
- `#` at the start of a line is a comment; blank lines are skipped.

A sample `config.txt` is committed at the repo root with the values shown above.

### `src/Console.cpp` — wiring (Track 3)

The existing `Console` already has the banner, the `initialize` gate, the dispatch table, and stub handlers. This track replaces four of those stubs with real work.

```cpp
// Replace "initialize command recognized. Doing something." with:
void Console::handleInitialize() {
    if (initialized_) {
        std::cout << "Already initialized.\n";
        return;
    }
    try {
        Config cfg = parseConfig("config.txt");
        Scheduler::instance().initialize(toSchedulerConfig(cfg));
        initialized_ = true;
        std::cout << "Initialized: " << cfg.numCpu << " cores, "
                  << (cfg.scheduler == Config::Algo::FCFS ? "FCFS" : "RR") << ".\n";
    } catch (const std::exception& e) {
        std::cout << "Failed to initialize: " << e.what() << "\n";
    }
}

// scheduler-start / scheduler-stop become one-liners:
void Console::handleSchedulerStart() { Scheduler::instance().startGenerator(); std::cout << "Generator started.\n"; }
void Console::handleSchedulerStop()  { Scheduler::instance().stopGenerator();  std::cout << "Generator stopped.\n"; }

// exit (in Console::run loop) must call shutdown before returning:
//   if (cmd == "exit") { if (initialized_) Scheduler::instance().shutdown(); break; }
```

`screen` and `report-util` stay as the current "command recognized" stubs — Track 4 owns them.

The dispatch table refactor: extract the if/else chain in `Console::dispatch` into named handler methods (`handleInitialize`, `handleSchedulerStart`, `handleSchedulerStop`, `handleClear`, plus stubs for `handleScreen`, `handleReportUtil`) so each command's behavior is one short function. This makes Track 4's later edits drop-in: they replace `handleScreen` / `handleReportUtil`, nothing else.

The Console process factory wiring is deferred. Track 1 ships with a default factory that returns `nullptr` and logs a one-line warning; Track 2 will call `Scheduler::instance().setProcessFactory(...)` from its module-init code. Until then, `scheduler-start` won't actually produce processes — that's intentional, and the warning makes it visible.

---

## Locking discipline

| Shared state | Guarded by | Writers | Readers |
|--------------|------------|---------|---------|
| `tick_` | atomic | tick thread | everyone |
| `running_`, `generatorRunning_` | atomic | Scheduler / generator control | engine threads |
| `ready_`, `runningProcs_`, `finishedProcs_`, `owned_`, `coresUsed_`, `nextPid_` | `stateMutex_` | engine + CPUs (via engine methods) | snapshot calls |
| `current_` (per CPU) | atomic | the owning CPU only | the owning CPU (writes), Track 4 read via `getCurrentProcess()` |
| `tickCv_` | `tickMutex_` | tick thread (notify) | CPUs (wait) |

**Rules**
- Never hold `stateMutex_` across a process method call. Pop → release lock → execute → re-acquire as needed.
- Snapshots copy under lock, return by value.
- `Scheduler::shutdown()` order: `stopGenerator()` → `engine_->stop()` → `cores_[*]->stop()` → destructors.

---

## File layout

```
csopesy-mco1/
├── CMakeLists.txt                       (edited — adds mco1_core lib + tests + demo)
├── config.txt                           (new — sample default values at repo root)
├── src/
│   ├── main.cpp                         (unchanged)
│   ├── Console.h / .cpp                 (edited — real initialize/exit/start/stop wiring)
│   ├── Config.h / .cpp                  (new — parser + Config struct + toSchedulerConfig)
│   ├── IProcess.h                       (new)
│   ├── SchedulingPolicy.h / .cpp        (new)
│   ├── CPU.h / .cpp                     (new)
│   ├── SchedulerEngine.h / .cpp         (new)
│   └── Scheduler.h / .cpp               (new)
├── tests/
│   ├── MockProcess.h / .cpp             (new — fake IProcess for tests)
│   ├── fixtures/
│   │   ├── config_valid.txt             (new — golden-path config file)
│   │   ├── config_bad_range.txt         (new — num-cpu out of range)
│   │   └── config_missing_key.txt       (new — missing batch-process-freq)
│   ├── test_config.cpp                  (parser + validation)
│   ├── test_policy.cpp                  (FCFS + RR pure logic)
│   ├── test_engine.cpp                  (stepOnce-driven engine tests)
│   └── test_scheduler.cpp               (full threaded integration)
├── demos/
│   └── scheduler_demo.cpp               (new — visual sanity check)
└── docs/
    └── superpowers/specs/
        └── 2026-05-26-track1-scheduler-design.md   (this doc)
```

---

## Test plan

### Tier 1 — Pure logic, no threads (target: < 50 ms total)

**`test_config.cpp`**
- `config_valid.txt` → all seven fields parsed correctly; `validate()` returns true.
- `config_bad_range.txt` (e.g., `num-cpu 0`) → `parseConfig` throws with message containing `"num-cpu"`.
- `config_missing_key.txt` → `parseConfig` throws with message containing the missing key name.
- `scheduler` field: `"fcfs"` → `Algo::FCFS`; `"rr"` → `Algo::RR`; `"sjf"` → throws.
- `min-ins > max-ins` → rejects.
- Comments (`# ...`) and blank lines are skipped.
- `toSchedulerConfig` correctly maps `Config::Algo` → `SchedulerConfig::Algo`.

**`test_policy.cpp`**
- `FCFSPolicy::pickNext` pops front; `shouldKeepRunning` true until finished/sleeping; `onPreempt` is no-op.
- `RRPolicy::pickNext` pops front; `shouldKeepRunning` false at `ticksOnCore == quantum`; `onPreempt` pushes to back.
- Both policies handle empty queue (`pickNext` returns `nullptr`).

**`test_engine.cpp`** (uses `stepOnce()` — no real tick thread)
- `enqueueReady` + `popReady` order matches FCFS expectations.
- `stepOnce()` increments `tick_` monotonically.
- Generator (under `stepOnce`-only mode): with `batchProcessFreq=3` and a factory plugged in, calling `stepOnce` 9 times produces exactly 3 generated processes.
- `snapshotRunning` / `snapshotFinished` return copies that don't reflect later mutations.
- Process names: 1st = `p01`, 100th = `p100`.

### Tier 2 — Real threading (target: each test < 1 s)

**`test_scheduler.cpp`**
- **FCFS-1core-3procs**: enqueue 3 MockProcesses (10 instr each), 1 core, FCFS → all finish in enqueue order.
- **FCFS-2core-4procs**: 4 equal-length mocks, 2 cores → both cores active, all finish.
- **RR-4core-quantum5**: 4 long mocks (50 instr), 4 cores, RR `quantum=5` → each MockProcess records its per-visit tick counts; assert every visit ≤ 5 ticks.
- **delays-per-exec**: 1 mock (10 instr), 1 core, `delaysPerExec=3` → tick at finish ≈ 10*(1+3) = 40 (±tolerance).
- **SLEEP semantics**: mock that requests sleep(5) at instr 3 on a 1-core FCFS run → finish-tick ≈ baseline + 5.
- **Generator**: `batchProcessFreq=2`, generator on for 20 ticks → ≈ 10 processes generated (±1).
- **Clean shutdown**: `initialize → startGenerator → shutdown` completes inside 500 ms; no thread leaks (verified by checking `std::thread::joinable()` is false after shutdown).

### `MockProcess` capabilities

- Constructible with `totalLines`, optional `sleepAtLine`, optional `sleepDuration`.
- `executeNext(tick)` increments `currentLine_`, appends `(tick, coreId-from-Scheduler)` to a recorded log (for assertions). Demo build also prints `[tick=N core=K p=NAME line=L/T]`.
- `tickSleep()` decrements an internal sleep counter; transitions to READY when zero.
- Thread-safe access to its log via internal mutex.

---

## Demo binary

`demos/scheduler_demo.cpp`:

1. Build a `SchedulerConfig` for FCFS, 4 cores, 6 mock processes (15 instructions each). Run for 3 seconds. Print scheduler state to stdout each tick.
2. Shutdown, then re-`initialize` with RR, quantum=3, same 6 processes. Run 3 seconds. Print again.
3. Exit.

This is the eyeball test — you can see ticks happening, cores sharing work, and quantum slicing.

---

## CMake changes

```cmake
# CMakeLists.txt — replaces current contents
cmake_minimum_required(VERSION 3.14)        # FetchContent needs 3.14+
project(CSOPESY-MCO1 CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

add_library(mco1_core
    src/Config.cpp
    src/SchedulingPolicy.cpp
    src/SchedulerEngine.cpp
    src/CPU.cpp
    src/Scheduler.cpp
)
target_include_directories(mco1_core PUBLIC src)
target_link_libraries(mco1_core PUBLIC Threads::Threads)

add_executable(mco1 src/main.cpp src/Console.cpp)
target_link_libraries(mco1 PRIVATE mco1_core)

add_executable(scheduler_demo demos/scheduler_demo.cpp)
target_link_libraries(scheduler_demo PRIVATE mco1_core)

option(BUILD_TESTING "Build unit tests" ON)
if(BUILD_TESTING)
    enable_testing()
    include(FetchContent)
    FetchContent_Declare(googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
    )
    FetchContent_MakeAvailable(googletest)

    add_executable(mco1_tests
        tests/MockProcess.cpp
        tests/test_config.cpp
        tests/test_policy.cpp
        tests/test_engine.cpp
        tests/test_scheduler.cpp
    )
    target_link_libraries(mco1_tests PRIVATE mco1_core GTest::gtest_main)
    target_compile_definitions(mco1_tests PRIVATE
        TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures"
    )
    add_test(NAME mco1_tests COMMAND mco1_tests)
endif()
```

`TEST_FIXTURES_DIR` lets `test_config.cpp` open fixture files by absolute path, so tests don't depend on the working directory of the runner.

---

## CLAUDE.md additions

Add a new section after "Architecture notes":

```
## Modularity rules (Track 1 enforced)

- Scheduler depends on `IProcess` only — never on Track 2's concrete `Process`.
- Scheduling policy decisions live in `ISchedulingPolicy` subclasses (`FCFSPolicy`, `RRPolicy`).
  Never put algorithm logic in `Scheduler`, `CPU`, or `SchedulerEngine`.
- `SchedulerEngine` owns all shared mutable state. CPUs and policies access it through
  engine methods only, never through raw queue references that escape the lock.
- Public snapshot methods return values, never references.
- Threading lives only in `CPU.cpp` and `SchedulerEngine.cpp`. `SchedulingPolicy.cpp`
  and `Scheduler.cpp` must compile without `<thread>` / `<mutex>` includes.
```

---

## Acceptance criteria

- [ ] Code compiles under C++20 with `-Wall -Wextra -Wpedantic`.
- [ ] `cmake --build . --target mco1` builds; the binary's banner / clear / unknown-command behavior is preserved.
- [ ] `mco1` REPL: typing `initialize` reads `config.txt`, prints the resolved config summary, and unblocks the gate.
- [ ] `mco1` REPL: typing `scheduler-start` starts the generator; `scheduler-stop` stops it; `exit` shuts down cleanly within 500 ms.
- [ ] `cmake --build . --target mco1_tests && ctest` passes all Tier 1 + Tier 2 tests (Config, Policy, Engine, Scheduler).
- [ ] `cmake --build . --target scheduler_demo && ./scheduler_demo` shows visible interleaving of cores and respects quantum/delays-per-exec.
- [ ] No data races (verified by `-fsanitize=thread` run on the test binary if available locally).
- [ ] CLAUDE.md updated with the modularity rules section above.
- [ ] All Tier 2 tests complete in under 5 seconds total wall-clock.
- [ ] A sample `config.txt` is committed at the repo root with valid default values.

---

## Out-of-scope notes for downstream tracks

- **Track 2:** `Process` must inherit from `IProcess` (this header). Provide a `ProcessFactory` callable to `Scheduler::setProcessFactory` (typical wiring: in `main.cpp` or a Track 2 init function called from `Console::handleInitialize` after `Scheduler::initialize`). Sleep mechanics: when `executeNext` encounters a `SLEEP` instruction, set state to `SLEEPING` and stash sleep ticks; `tickSleep()` decrements; transition back to `READY` when zero. Track 2 reads `minIns` / `maxIns` from the same `Config` struct that this spec defines.
- **Track 4:** Read via the snapshot methods on `Scheduler::instance()`. Never call the engine directly. The Console handlers for `screen` and `report-util` are left as the existing stubs — replace them in `Console.cpp`, not by editing the dispatch table elsewhere.
