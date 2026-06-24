#include "cli/Reporter.h"

#include "scheduler/IProcess.h"
#include "scheduler/Scheduler.h"

#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string formatTime(std::chrono::system_clock::time_point tp) {
    const std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%m/%d/%Y %I:%M:%S%p");
    return oss.str();
}

std::string stateLabel(ProcessState state) {
    switch (state) {
        case ProcessState::READY: return "READY";
        case ProcessState::RUNNING: return "RUNNING";
        case ProcessState::SLEEPING: return "SLEEPING";
        case ProcessState::FINISHED: return "FINISHED";
    }
    return "UNKNOWN";
}

void appendProcessList(std::ostringstream& oss, const std::vector<IProcess*>& processes) {
    if (processes.empty()) {
        oss << "  None\n";
        return;
    }

    for (const IProcess* p : processes) {
        if (!p) continue;
        oss << "  " << p->getName()
            << "  (" << formatTime(p->getCreatedAt()) << ")"
            << "  Core: N/A"
            << "  " << p->getCurrentLine() << " / " << p->getTotalLines()
            << "  " << stateLabel(p->getState()) << "\n";
    }
}

}  // namespace

std::string Reporter::buildReport() {
    Scheduler& scheduler = Scheduler::instance();

    std::ostringstream oss;
    oss << "CPU utilization: "
        << std::fixed << std::setprecision(2) << scheduler.getCpuUtilization() << "%\n";
    oss << "Cores used: " << scheduler.getCoresUsed() << "\n";
    oss << "Cores available: "
        << (scheduler.getCoresTotal() - scheduler.getCoresUsed())
        << " / " << scheduler.getCoresTotal() << "\n";
    oss << "\n";

    oss << "Running processes:\n";
    appendProcessList(oss, scheduler.getRunningSnapshot());
    oss << "\n";

    oss << "Finished processes:\n";
    appendProcessList(oss, scheduler.getFinishedSnapshot());

    return oss.str();
}

bool Reporter::writeReport(const std::string& path, std::ostream& out) {
    std::ofstream log(path, std::ios::app);
    if (!log) {
        out << "Failed to write report to " << path << ".\n";
        return false;
    }

    const std::string report = buildReport();

    log << "Report generated at: " << formatTime(std::chrono::system_clock::now()) << "\n";
    log << report;
    log << "\n";

    out << report;
    out << "Report written to " << path << ".\n";
    return true;
}
