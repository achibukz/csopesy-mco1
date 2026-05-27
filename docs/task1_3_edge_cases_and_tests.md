# Task 1 & 3: Edge Cases and Test Scenarios

This document outlines potential edge cases, stress tests, and unverified scenarios for the Process Scheduler OS Emulator (Tasks 1 & 3). These can be used by Claude or developers to expand the test suite and ensure ultimate robustness.

## Task 1: Scheduler & Scheduling Policies (FCFS, RR)

### 1. Quantum & Completion Synchronization
*   **Scenario:** A process running under Round Robin finishes its very last instruction on the *exact same tick* its quantum expires.
*   **Expected Behavior:** The process should be marked `FINISHED` and NOT placed back into the `READY` queue. The current implementation handles `FINISHED` first, but a specific integration test should verify this timing.

### 2. Extreme Quantum Values
*   **Quantum = 1 (Thrashing):** Test RR with `quantum = 1`. This forces a context switch every single tick. Does the overhead or queue logic break down? Does the CPU utilization calculation remain accurate?
*   **Quantum > Process Length:** Set quantum to 1000 for a process with 5 instructions. RR should effectively degrade into FCFS.

### 3. Immediate Completion / Zero-Length Processes
*   **Scenario:** A process is enqueued with 0 total lines of code (if the config/generator allows it).
*   **Expected Behavior:** It should be immediately marked `FINISHED` upon selection, without consuming a full tick of execution, or rejected at creation.

### 4. Simultaneous Awakenings
*   **Scenario:** Multiple sleeping processes have their sleep timers expire on the exact same tick.
*   **Expected Behavior:** The `SchedulerEngine` should move all of them from `SLEEPING` to `READY` safely without race conditions or dropping pointers.

### 5. Starvation / Queue Saturation
*   **Scenario:** Enqueue 10,000 processes on a 1-core FCFS configuration.
*   **Expected Behavior:** Ensure the `std::queue` does not cause memory bloat and that the core efficiently processes them one by one without crashing.

---

## Task 3: Threading, Configuration, and Console

### 1. Thread Exhaustion & Limits
*   **Scenario:** The config allows up to 128 cores (`numCpu = 128`).
*   **Expected Behavior:** Spawning 128 `std::thread` instances for CPUs (plus the tick and generator threads) should not crash the host OS or hit `std::system_error` for thread limits. A stress test should verify graceful handling if thread creation fails.

### 2. Extreme Configuration Values
*   **Scenario:** Set `delays-per-exec` to a massive number (e.g., 1,000,000).
*   **Expected Behavior:** The simulation should not overflow integer counters when calculating the next execution tick, and CPU utilization should reflect the heavy idle/wait time accurately.

### 3. Configuration Parsing Oddities
*   **Scenario:** `config.txt` contains carriage returns (`\r\n` vs `\n`), trailing spaces after values, or multiple consecutive blank lines.
*   **Expected Behavior:** The parser (`parseConfigFromString`) should trim safely and not throw a `ConfigError` due to invisible whitespace. (Current tests check basic whitespace, but aggressive formatting should be tested).

### 4. REPL / Console Edge Cases
*   **Scenario:** The user mashes `ENTER` repeatedly, inputs strings longer than terminal buffers, or inputs commands with excessive spaces (e.g., `    screen     -s     `).
*   **Expected Behavior:** The console loop should ignore empty inputs safely and tokenize commands robustly without segmentation faults.

### 5. Asynchronous `report-util`
*   **Scenario:** The user types `report-util` while 128 cores are actively popping and pushing to the queues.
*   **Expected Behavior:** Reading the snapshots (`snapshotRunning`, `snapshotFinished`) and calculating utilization must remain thread-safe. Iterating over the running processes vector while a CPU thread is modifying it could cause a data race if the mutex isn't held correctly during the snapshot.
