# MCO1 Code Reference — C++ Class Skeletons & Interface Contracts

_Generated: 2026-05-26_

Companion document to `2026-05-26-CSOPESY-mco1-group-plan.md`. Contains C++ class structures, interface signatures, and pseudocode for each track. **Lock these signatures on Day 1** so each person can compile and test their track in isolation.

**Target repo:** [github.com/achibukz/csopesy-mco1](https://github.com/achibukz/csopesy-mco1) (local: `/Users/achibukz/Code/GitHub/csopesy-mco1`)

**Naming convention** follows the existing repo's `CLAUDE.md` plan:
- `Instructions.h` holds `IInstruction` base + all 6 instruction subclasses (not split into separate files)
- `Scheduler.h/cpp` contains abstract `IScheduler` + `FCFSScheduler` + `RRScheduler`
- `CPU.h/cpp` (not `CPU`) — one class per CPU core worker thread

---

## Architecture Diagram

```
┌────────────────────────────────────────────────────────────────┐
│                     MAIN LOOP (Person C)                       │
│  while (running) { handleInput(); dispatch(); refresh(); }     │
└──────────────┬─────────────────────────────────────────────────┘
               │
               ├──► Console + Parser (Person C)
               │       └─ initialize, exit, scheduler-start/stop,
               │          config.txt, top-level dispatcher
               │
               ├──► Screen Multiplexer (Person D)
               │       └─ screen -s/-r/-ls, process-smi,
               │          report-util, csopesy-log.txt
               │
               ├──► Scheduler Core (Person A) ◄─────┐
               │       └─ FCFS/RR, std::thread cores,│
               │          ready queue, tick counter, │ shared state
               │          batch-process-freq         │ (mutex-protected)
               │                                     │
               └──► Process + Instructions (Person B) ◄─┘
                       └─ Process class, IInstruction,
                          PRINT/DECLARE/ADD/SUB/SLEEP/FOR,
                          variable engine, randomized instr gen
```

---

## Track 1 — Scheduler Core (Person A)

### CPU class

```cpp
class CPU {
    int coreID;
    std::thread workerThread;
    std::atomic<Process*> currentProcess{nullptr};
    std::atomic<bool> busy{false};

    void run();  // worker loop: pick from ready queue, execute instructions

public:
    CPU(int id);
    void start();
    void stop();
    bool isBusy() const { return busy.load(); }
    Process* getCurrentProcess() const { return currentProcess.load(); }
};
```

### Scheduler class (singleton)

```cpp
enum class SchedulerType { FCFS, RR };

class Scheduler {
    std::vector<CPU> cores;
    std::queue<Process*> readyQueue;
    std::vector<Process*> runningProcs;
    std::vector<Process*> finishedProcs;
    std::mutex queueMutex;
    std::condition_variable queueCV;
    std::atomic<uint64_t> cpuTick{0};

    SchedulerType algo;
    uint32_t quantum;
    uint32_t delaysPerExec;
    uint32_t batchProcessFreq;

    std::thread generatorThread;
    std::atomic<bool> generatorRunning{false};
    std::thread tickThread;
    std::atomic<bool> running{false};

    void tickLoop();
    void generatorLoop();

public:
    static Scheduler& instance();

    void initialize(const Config& cfg);
    void start();
    void stop();

    void enqueue(Process* p);
    void startGenerator();
    void stopGenerator();

    // Read-only getters (thread-safe snapshots for Person D)
    double getCpuUtilization() const;
    int getCoresUsed() const;
    int getCoresTotal() const;
    std::vector<Process*> getRunningSnapshot() const;
    std::vector<Process*> getFinishedSnapshot() const;
    uint64_t getCurrentTick() const { return cpuTick.load(); }
};
```

### FCFS algorithm (pseudocode)

```
loop:
    process = readyQueue.pop_front()
    while process.not_finished() and not process.is_sleeping():
        process.executeNext(currentTick)
        sleep(delaysPerExec ticks)  // busy-wait, still holding CPU
    if process.finished():
        move to finishedProcs
    else if process.sleeping():
        // handled separately by sleep manager
```

### Round Robin algorithm (pseudocode)

```
loop:
    process = readyQueue.pop_front()
    quantumRemaining = quantum
    while quantumRemaining > 0 and process.not_finished() and not process.is_sleeping():
        process.executeNext(currentTick)
        sleep(delaysPerExec ticks)
        quantumRemaining -= 1
    if process.finished():
        move to finishedProcs
    else if process.sleeping():
        // sleep manager handles re-enqueue when wake
    else:
        readyQueue.push_back(process)  // preempted, back of queue
```

### Auto-generator loop (pseudocode)

```
while generatorRunning:
    wait until currentTick % batchProcessFreq == 0
    name = "p" + zero_padded(nextID++)
    process = InstructionGenerator::generate(name, pid, config.minIns, config.maxIns)
    enqueue(process)
```

---

## Track 2 — Process & Instructions (Person B)

### Process class

```cpp
enum class ProcessState { READY, RUNNING, SLEEPING, FINISHED };

class Process {
    int processID;
    std::string processName;
    int currentLine{0};
    int totalInstructions;
    std::vector<std::unique_ptr<IInstruction>> commandList;

    std::unordered_map<std::string, uint16_t> variables;
    std::vector<std::string> printLog;
    mutable std::mutex stateMutex;

    std::chrono::system_clock::time_point createdAt;
    std::atomic<ProcessState> state{ProcessState::READY};
    int sleepTicksRemaining{0};

public:
    Process(int id, std::string name, std::vector<std::unique_ptr<IInstruction>> cmds);

    // Called by Person A's CPU
    void executeNext(uint64_t currentTick);
    void tickSleep();  // decrement sleepTicksRemaining; wake if zero

    // Read-only getters (thread-safe)
    int getPID() const { return processID; }
    std::string getName() const { return processName; }
    ProcessState getState() const { return state.load(); }
    int getCurrentLine() const { return currentLine; }
    int getTotalLines() const { return totalInstructions; }
    std::vector<std::string> getPrintLog() const;  // returns a copy
    std::chrono::system_clock::time_point getCreatedAt() const { return createdAt; }
    bool isFinished() const { return state.load() == ProcessState::FINISHED; }

    // Used by IInstruction subclasses to mutate variables
    uint16_t getVar(const std::string& name);  // auto-declares as 0 if missing
    void setVar(const std::string& name, uint16_t value);
    void appendPrint(const std::string& msg);
    void requestSleep(uint8_t ticks);
};
```

### IInstruction interface

```cpp
class IInstruction {
public:
    virtual void execute(Process* p) = 0;
    virtual ~IInstruction() = default;
};
```

### Six concrete commands

```cpp
class PrintCommand : public IInstruction {
    std::string message;       // literal portion
    std::string varRef;        // optional variable to concatenate (empty if none)
public:
    PrintCommand(std::string msg, std::string var = "");
    void execute(Process* p) override;
    // Default behavior: if message is empty, use "Hello world from <process_name>!"
};

class DeclareCommand : public IInstruction {
    std::string varName;
    uint16_t value;
public:
    void execute(Process* p) override { p->setVar(varName, value); }
};

struct Operand {
    bool isLiteral;
    uint16_t literal;
    std::string varName;
};

class AddCommand : public IInstruction {
    std::string target;
    Operand op2, op3;
public:
    void execute(Process* p) override {
        uint32_t a = op2.isLiteral ? op2.literal : p->getVar(op2.varName);
        uint32_t b = op3.isLiteral ? op3.literal : p->getVar(op3.varName);
        uint32_t result = a + b;
        if (result > 65535) result = 65535;  // clamp, not wrap
        p->setVar(target, static_cast<uint16_t>(result));
    }
};

class SubtractCommand : public IInstruction {
    std::string target;
    Operand op2, op3;
public:
    void execute(Process* p) override {
        int32_t a = op2.isLiteral ? op2.literal : p->getVar(op2.varName);
        int32_t b = op3.isLiteral ? op3.literal : p->getVar(op3.varName);
        int32_t result = a - b;
        if (result < 0) result = 0;
        if (result > 65535) result = 65535;
        p->setVar(target, static_cast<uint16_t>(result));
    }
};

class SleepCommand : public IInstruction {
    uint8_t ticks;
public:
    SleepCommand(uint8_t t) : ticks(t) {}
    void execute(Process* p) override { p->requestSleep(ticks); }
};

class ForCommand : public IInstruction {
    std::vector<std::unique_ptr<IInstruction>> body;
    int repeats;
public:
    void execute(Process* p) override {
        for (int i = 0; i < repeats; ++i) {
            for (auto& cmd : body) cmd->execute(p);
        }
    }
};
```

### InstructionGenerator

```cpp
class InstructionGenerator {
public:
    static Process* generate(const std::string& name, int pid,
                             uint32_t minIns, uint32_t maxIns);

private:
    static std::unique_ptr<IInstruction> randomCommand(int depthRemaining);
    // depthRemaining starts at 3; FOR reduces it by 1 for nested generation
};
```

---

## Track 3 — Console + Config (Person C)

### Config struct

```cpp
struct Config {
    int numCpu;              // [1, 128]
    std::string scheduler;   // "fcfs" or "rr"
    uint32_t quantumCycles;
    uint32_t batchProcessFreq;
    uint32_t minIns;
    uint32_t maxIns;
    uint32_t delaysPerExec;

    bool validate() const;
};

Config parseConfig(const std::string& path);
```

### Sample config.txt format

```
num-cpu 4
scheduler rr
quantum-cycles 5
batch-process-freq 1
min-ins 1000
max-ins 2000
delays-per-exec 0
```

### Console class

```cpp
class Console {
    std::atomic<bool> running{true};
    std::atomic<bool> initialized{false};
    std::string currentScreen;  // empty = main menu

public:
    void printHeader();
    void refresh();
    bool hasInput() const;
    std::string readLine();
    void dispatch(const std::string& line);
    bool isRunning() const { return running.load(); }

private:
    void handleInitialize();
    void handleScreen(const std::vector<std::string>& tokens);
    void handleSchedulerStart();
    void handleSchedulerStop();
    void handleReportUtil();
    void handleClear();
    void handleExit();
};
```

### Main loop

```cpp
int main() {
    Console console;
    console.printHeader();
    while (console.isRunning()) {
        console.refresh();
        if (console.hasInput()) {
            std::string line = console.readLine();
            console.dispatch(line);
        }
    }
    Scheduler::instance().stop();
    return 0;
}
```

### Initialization gate pseudocode

```cpp
void Console::dispatch(const std::string& line) {
    auto tokens = tokenize(line);
    if (tokens.empty()) return;

    const std::string& cmd = tokens[0];

    if (!initialized.load() && cmd != "initialize" && cmd != "exit") {
        std::cout << "Please initialize first.\n";
        return;
    }

    if (cmd == "initialize")           handleInitialize();
    else if (cmd == "exit")            handleExit();
    else if (cmd == "screen")          handleScreen(tokens);
    else if (cmd == "scheduler-start") handleSchedulerStart();
    else if (cmd == "scheduler-stop")  handleSchedulerStop();
    else if (cmd == "report-util")     handleReportUtil();
    else if (cmd == "clear")           handleClear();
    else                                std::cout << "Unknown command.\n";
}
```

---

## Track 4 — Screen Multiplexer + Reporting (Person D)

### ScreenManager class

```cpp
class ScreenManager {
    std::unordered_map<std::string, Process*> screens;
    std::mutex screensMutex;
    std::string currentScreen;  // empty = main menu

public:
    void create(const std::string& name);        // screen -s
    void reattach(const std::string& name);      // screen -r
    void listAll();                              // screen -ls
    void processSmi();                           // inside-screen process-smi
    void exitScreen();                           // back to main menu
    bool inScreen() const { return !currentScreen.empty(); }
};
```

### `screen -s` pseudocode

```cpp
void ScreenManager::create(const std::string& name) {
    if (screens.count(name)) {
        std::cout << "Process " << name << " already exists.\n";
        return;
    }
    Process* p = InstructionGenerator::generate(name, nextPID++, minIns, maxIns);
    {
        std::lock_guard<std::mutex> lk(screensMutex);
        screens[name] = p;
    }
    Scheduler::instance().enqueue(p);
    currentScreen = name;
    clearTerminal();
    showProcessHeader(p);
}
```

### `screen -r` pseudocode

```cpp
void ScreenManager::reattach(const std::string& name) {
    std::lock_guard<std::mutex> lk(screensMutex);
    auto it = screens.find(name);
    if (it == screens.end() || it->second->isFinished()) {
        std::cout << "Process " << name << " not found.\n";
        return;
    }
    currentScreen = name;
    clearTerminal();
    showProcessHeader(it->second);
}
```

### `screen -ls` output template

```
CPU utilization: 75%
Cores used: 3
Cores available: 4

Running processes:
  p01  (2026-05-26 14:33:12)  Core: 0   12 / 100
  p02  (2026-05-26 14:33:14)  Core: 1   45 / 80
  p05  (2026-05-26 14:33:20)  Core: 2   3 / 200

Finished processes:
  p03  (2026-05-26 14:33:15)  Finished   80 / 80
  p04  (2026-05-26 14:33:18)  Finished   60 / 60
```

### `process-smi` pseudocode

```cpp
void ScreenManager::processSmi() {
    auto* p = screens[currentScreen];
    std::cout << "Process: " << p->getName() << "\n";
    std::cout << "ID: " << p->getPID() << "\n";
    std::cout << "State: " << stateToString(p->getState()) << "\n";
    std::cout << "Logs:\n";
    for (const auto& msg : p->getPrintLog()) std::cout << "  " << msg << "\n";
    if (p->isFinished()) std::cout << "Finished!\n";
}
```

### Reporter / `report-util`

```cpp
class Reporter {
public:
    static void writeUtilization();  // appends to csopesy-log.txt with timestamp
};
```

Output format mirrors `screen -ls` but writes to `csopesy-log.txt` (append mode, with timestamp header).

---

## Critical Shared Interface Contracts

These signatures must be locked before parallel work begins:

```cpp
// Person B exposes (Person A and Person D call these):
class Process {
    int getPID() const;
    std::string getName() const;
    ProcessState getState() const;
    int getCurrentLine() const;
    int getTotalLines() const;
    std::vector<std::string> getPrintLog() const;
    std::chrono::system_clock::time_point getCreatedAt() const;
    bool isFinished() const;
    void executeNext(uint64_t tick);  // called only by Person A's CPU
};

class InstructionGenerator {
    static Process* generate(const std::string& name, int pid,
                             uint32_t minIns, uint32_t maxIns);
};

// Person A exposes (Person C and Person D call these):
class Scheduler {
    static Scheduler& instance();
    void initialize(const Config& cfg);
    void enqueue(Process* p);
    void startGenerator();
    void stopGenerator();
    double getCpuUtilization() const;
    int getCoresUsed() const;
    int getCoresTotal() const;
    std::vector<Process*> getRunningSnapshot() const;
    std::vector<Process*> getFinishedSnapshot() const;
};
```

---

## Threading & Synchronization Cheatsheet

| Shared State | Protected By | Accessed By |
|--------------|--------------|-------------|
| `Scheduler::readyQueue` | `queueMutex` + `queueCV` | A (write), A (read) |
| `Scheduler::runningProcs` | `queueMutex` | A (write), D (read snapshot) |
| `Scheduler::finishedProcs` | `queueMutex` | A (write), D (read snapshot) |
| `Scheduler::cpuTick` | `std::atomic<uint64_t>` | A (write), all (read) |
| `Process::state` | `std::atomic<ProcessState>` | B (write), A/D (read) |
| `Process::printLog` | `stateMutex` | B (write), D (read copy) |
| `Process::variables` | `stateMutex` | B only |
| `ScreenManager::screens` | `screensMutex` | D only |
| `Console::initialized` | `std::atomic<bool>` | C only |

**Rule of thumb:** never return a reference to mutable shared state. Always return copies/snapshots.

---

## Recommended Project Layout

Matches the existing `csopesy-mco1` repo's planned layout:

```
csopesy-mco1/
├── CMakeLists.txt              (bump to C++20)
├── README.md
├── config.txt                  (default values, root level)
├── src/
│   ├── main.cpp                (Person C — entry point only)
│   ├── Console.h/.cpp          (Person C — main menu, command dispatch)
│   ├── Config.h/.cpp           (Person C — config.txt parser)
│   ├── Scheduler.h/.cpp        (Person A — IScheduler + FCFS + RR)
│   ├── CPU.h/.cpp              (Person A — worker thread per core)
│   ├── Process.h/.cpp          (Person B — process state, variables, log)
│   ├── Instructions.h          (Person B — IInstruction + 6 types + generator)
│   ├── ScreenManager.h/.cpp    (Person D — screen -s/-r/-ls, process-smi)
│   └── Reporter.h/.cpp         (Person D — report-util, csopesy-log.txt)
```

**Build & run (after bumping to C++20):**
```bash
cmake . -DCMAKE_BUILD_TYPE=Debug
make
./mco1
```
