# Code Review Report: Scheduling Feature (Tracks 1 & 3)

**Date:** 2026-05-26  
**Status:** Approved with Minor Observations  
**Reviewer:** Gemini CLI

---

## Executive Summary

The implementation of the Scheduler Core, CPU Threading, and Console Wiring (Tracks 1 & 3) has been reviewed against the design specifications and group plan. The feature is functionally correct, passes all unit tests, and exhibits expected behavior in visual demos. One minor architectural deviation regarding locking discipline was identified.

## Functional Verification

| Test Suite | Result | Notes |
| :--- | :--- | :--- |
| **ConfigTest** | ✅ PASS | Correctly parses `config.txt`, validates ranges, and handles errors. |
| **PolicyTest** | ✅ PASS | FCFS and Round Robin logic correctly implemented. |
| **EngineTest** | ✅ PASS | Core tick logic and process generation work as expected. |
| **SchedulerFixture** | ✅ PASS | Threaded integration tests for FCFS, RR, Sleep, and Quantum slicing pass. |
| **Visual Demo** | ✅ PASS | `scheduler_demo` confirms cores interleaving and preemption. |

## Specification Adherence

| Requirement | Adherence | Notes |
| :--- | :--- | :--- |
| **Single Tick Source** | ✅ Full | Only `SchedulerEngine` drives the logical clock. |
| **Strategy Pattern** | ✅ Full | Algorithms are encapsulated in `ISchedulingPolicy` subclasses. |
| **Initialization Gate** | ✅ Full | Console blocks commands until `initialize` is called. |
| **Thread Safety** | ✅ Full | Uses atomics, mutexes, and snapshots for safe state access. |
| **Locking Discipline** | ⚠️ Partial | Found one instance of holding a lock across a virtual call. |

## Identified Observations & "Mistakes"

### 1. Locking Discipline Violation (Critical for Handoff)
**File:** `src/SchedulerEngine.cpp`  
**Method:** `tickSleepingProcesses()`

**Observation:**
The code currently calls `p->tickSleep()` while holding the `stateMutex_`. 

**The Risk:**
This violates the core design principle: *"Never hold stateMutex_ across a process method call."* If Track 2's implementation of `tickSleep()` attempts to call any Scheduler method (e.g., to check CPU utilization), it will attempt to re-acquire the `stateMutex_`, resulting in a **deadlock**.

**Recommendation:**
Refactor the method to "collect, release, tick, re-acquire":
1. Snapshot the `sleepingProcs_` list under lock.
2. Iterate and call `p->tickSleep()` outside the lock.
3. Re-acquire the lock to move `READY` processes back to the ready queue.

### 2. Quantum Zero Handling
**File:** `src/SchedulingPolicy.cpp`  
**Observation:**
In `RRPolicy::shouldKeepRunning`, there is a check for `quantum == 0`. While the config validator prevents this, the policy code correctly handles it by treating it as "infinite quantum" (non-preemptive). This is good defensive coding.

### 3. Sleep Design Improvement
**Observation:**
The implementation improves upon the spec by moving sleeping processes to a dedicated pool instead of keeping them on the core. This is a positive deviation as it increases CPU efficiency.

---

## Action Items for Track 2 & 4 Integration

1.  **Track 2 (Process Engine):** Ensure `Process::tickSleep()` remains lightweight and does not attempt to acquire `Scheduler` locks until the fix in Observation #1 is applied.
2.  **Track 4 (Reporting):** Use the `snapshotRunning()` and `snapshotFinished()` methods exclusively; they are verified thread-safe.

---
**Verdict:** The feature is fundamentally sound and ready for integration. The locking issue should be addressed before final submission to prevent complex deadlock bugs in a multi-track environment.
