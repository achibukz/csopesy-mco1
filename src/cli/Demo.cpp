#include "cli/Demo.h"

#include "config/Config.h"
#include "scheduler/IProcess.h"
#include "scheduler/Scheduler.h"

#include <atomic>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

class DemoProcess : public IProcess {
public:
    DemoProcess(int pid, std::string name, int totalInstructions, std::ostream& out, std::mutex& outMtx)
        : pid_(pid), name_(std::move(name)), total_(totalInstructions), out_(out), outMtx_(outMtx) {}

    int           getPID() const override         { return pid_; }
    std::string   getName() const override        { return name_; }
    ProcessState  getState() const override       { return state_.load(); }
    int           getCurrentLine() const override { return current_.load(); }
    int           getTotalLines() const override  { return total_; }
    bool          isFinished() const override     { return state_.load() == ProcessState::FINISHED; }

    void executeNext(uint64_t currentTick) override {
        int line = current_.fetch_add(1) + 1;
        if (line >= total_) state_.store(ProcessState::FINISHED);
        else                state_.store(ProcessState::RUNNING);
        std::lock_guard<std::mutex> lk(outMtx_);
        out_ << "[tick=" << std::setw(4) << currentTick
             << " p=" << name_
             << " line=" << std::setw(2) << line << "/" << total_ << "]\n";
    }

    void tickSleep() override {}

    std::vector<std::string>              getPrintLog() const override { return {}; }
    std::chrono::system_clock::time_point getCreatedAt() const override { return createdAt_; }

private:
    int                       pid_;
    std::string               name_;
    int                       total_;
    std::ostream&             out_;
    std::mutex&               outMtx_;
    std::atomic<int>          current_{0};
    std::atomic<ProcessState> state_{ProcessState::READY};
    std::chrono::system_clock::time_point createdAt_{std::chrono::system_clock::now()};
};

void runScenario(std::ostream& out,
                 const std::string& label,
                 SchedulerConfig::Algo algo,
                 uint32_t quantum) {
    out << "\n--- " << label << " ---\n";

    SchedulerConfig cfg;
    cfg.numCpu = 4;
    cfg.algo = algo;
    cfg.quantum = quantum;
    cfg.delaysPerExec = 0;
    cfg.batchProcessFreq = 100;  // generator disabled by default; high freq just in case

    Scheduler::instance().initialize(cfg);

    std::mutex outMtx;
    std::vector<std::unique_ptr<DemoProcess>> procs;
    for (int i = 1; i <= 6; ++i) {
        procs.push_back(std::make_unique<DemoProcess>(i, "p" + (i < 10 ? std::string("0") : std::string()) + std::to_string(i), 12, out, outMtx));
        Scheduler::instance().enqueue(procs.back().get());
    }

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline) {
        if (Scheduler::instance().getFinishedSnapshot().size() >= procs.size()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    {
        std::lock_guard<std::mutex> lk(outMtx);
        out << "(finished: " << Scheduler::instance().getFinishedSnapshot().size()
            << "/" << procs.size()
            << ", tick=" << Scheduler::instance().getCurrentTick() << ")\n";
    }

    Scheduler::instance().shutdown();
}

}  // namespace

namespace demo {

void run(std::ostream& out) {
    out << "Scheduler demo: FCFS then RR, 4 cores, 6 processes of 12 instructions each.\n";
    runScenario(out, "FCFS",                  SchedulerConfig::Algo::FCFS, 1);
    runScenario(out, "Round Robin (q=3)",     SchedulerConfig::Algo::RR,   3);
    out << "Demo complete.\n";
}

}  // namespace demo
