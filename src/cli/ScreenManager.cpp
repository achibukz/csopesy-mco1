#include "cli/ScreenManager.h"

#include "cli/Reporter.h"
#include "scheduler/Scheduler.h"

#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>

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
        case ProcessState::WAITING: return "WAITING";
        case ProcessState::FINISHED: return "FINISHED";
    }
    return "UNKNOWN";
}

}  // namespace

void ScreenManager::handleScreenCommand(const std::vector<std::string>& tokens,
                                        std::ostream& out) {
    if (tokens.size() < 2) {
        out << "Usage: screen -s <name> | screen -r <name> | screen -ls\n";
        return;
    }

    const std::string& option = tokens[1];
    if (option == "-ls") {
        out << Reporter::buildReport();
        return;
    }

    if (tokens.size() < 3) {
        out << "Usage: screen " << option << " <name>\n";
        return;
    }

    const std::string& name = tokens[2];
    if (option == "-s") {
        if (processesByName_.count(name)) {
            out << "Process " << name << " already exists.\n";
            return;
        }

        IProcess* process = Scheduler::instance().createProcess(name);
        if (!process) {
            out << "Unable to create process " << name << ".\n";
            return;
        }

        processesByName_[name] = process;
        current_ = process;
        showProcessScreen(*process, out);
        return;
    }

    if (option == "-r") {
        IProcess* process = findTracked(name);
        if (!process || process->isFinished()) {
            out << "Process " << name << " not found.\n";
            return;
        }

        current_ = process;
        showProcessScreen(*process, out);
        return;
    }

    out << "Unknown screen option: " << option << "\n";
}

void ScreenManager::handleProcessSmi(std::ostream& out) const {
    if (!current_) {
        out << "No active process screen.\n";
        return;
    }

    out << "Process name: " << current_->getName() << "\n";
    out << "PID: " << current_->getPID() << "\n";
    out << "State: " << stateLabel(current_->getState()) << "\n";
    out << "Current instruction line: "
        << current_->getCurrentLine() << " / " << current_->getTotalLines() << "\n";
    out << "Logs:\n";

    const auto logs = current_->getPrintLog();
    if (logs.empty()) {
        out << "  <no PRINT logs yet>\n";
    } else {
        for (const auto& line : logs) out << "  " << line << "\n";
    }

    if (current_->isFinished()) out << "Finished!\n";
}

void ScreenManager::exitScreen(std::ostream& out) {
    current_ = nullptr;
    out << "Returned to main menu.\n";
}

void ScreenManager::showProcessScreen(IProcess& process, std::ostream& out) const {
    out << "Process screen\n";
    out << "Name: " << process.getName() << "\n";
    out << "PID: " << process.getPID() << "\n";
    out << "Created: " << formatTime(process.getCreatedAt()) << "\n";
    out << "Current instruction line: "
        << process.getCurrentLine() << " / " << process.getTotalLines() << "\n";
}

IProcess* ScreenManager::findTracked(const std::string& name) const {
    auto it = processesByName_.find(name);
    if (it != processesByName_.end()) return it->second;

    for (IProcess* process : Scheduler::instance().getAllSnapshot()) {
        if (process && process->getName() == name) return process;
    }
    return nullptr;
}
