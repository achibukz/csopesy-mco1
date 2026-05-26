#include "Console.h"

#include "Config.h"
#include "Demo.h"
#include "Scheduler.h"

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <vector>

#ifdef _WIN32
#define CLEAR_CMD "cls"
#else
#define CLEAR_CMD "clear"
#endif

namespace {

std::vector<std::string> tokenize(const std::string& line) {
    std::vector<std::string> out;
    std::istringstream iss(line);
    std::string tok;
    while (iss >> tok) out.push_back(tok);
    return out;
}

const char* algoLabel(SchedulerConfig::Algo a) {
    return a == SchedulerConfig::Algo::FCFS ? "FCFS" : "RR";
}

}  // namespace

void Console::printHeader() {
    std::cout << "  ____ ____   ___  ____  _____ ______   __\n";
    std::cout << " / ___/ ___| / _ \\|  _ \\| ____/ ___\\ \\ / /\n";
    std::cout << "| |   \\___ \\| | | | |_) |  _| \\___ \\\\ V / \n";
    std::cout << "| |___ ___) | |_| |  __/| |___ ___) || |  \n";
    std::cout << " \\____|____/ \\___/|_|   |_____|____/ |_|  \n";
    std::cout << "\n";
    std::cout << "Hello, Welcome to CSOPESY commandline!\n";
    std::cout << "Type 'exit' to quit, 'clear' to clear the screen\n";
    std::cout << "\n";
    std::cout << "** IMPORTANT: Type 'initialize' to load config and start system **\n";
    std::cout << "\n";
}

void Console::clearScreen() {
    std::system(CLEAR_CMD);
    printHeader();
}

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
                  << algoLabel(toSchedulerConfig(cfg).algo)
                  << ", quantum=" << cfg.quantumCycles
                  << ", delays-per-exec=" << cfg.delaysPerExec
                  << ", batch-process-freq=" << cfg.batchProcessFreq << ".\n";
        std::cout << "(Note: no process factory registered yet; Track 2 wires this. "
                     "'scheduler-start' will not produce processes until then.)\n";
    } catch (const std::exception& e) {
        std::cout << "Failed to initialize: " << e.what() << "\n";
    }
}

void Console::handleSchedulerStart() {
    Scheduler::instance().startGenerator();
    std::cout << "Generator started.\n";
}

void Console::handleSchedulerStop() {
    Scheduler::instance().stopGenerator();
    std::cout << "Generator stopped.\n";
}

void Console::handleDemo() {
    if (initialized_) {
        Scheduler::instance().shutdown();
        initialized_ = false;
    }
    demo::run(std::cout);
    std::cout << "(Scheduler state was reset by the demo. Run 'initialize' to start fresh.)\n";
}

void Console::handleScreen(const std::vector<std::string>&) {
    std::cout << "screen command recognized. (Track 4)\n";
}

void Console::handleReportUtil() {
    std::cout << "report-util command recognized. (Track 4)\n";
}

void Console::handleExit() {
    if (initialized_) {
        Scheduler::instance().shutdown();
        initialized_ = false;
    }
}

void Console::dispatch(const std::string& line) {
    auto tokens = tokenize(line);
    if (tokens.empty()) return;
    const std::string& cmd = tokens[0];

    if (cmd == "clear") {
        clearScreen();
        return;
    }

    if (cmd != "initialize" && cmd != "exit" && !initialized_) {
        std::cout << "System not initialized. Run 'initialize' first.\n";
        return;
    }

    if (cmd == "initialize")           handleInitialize();
    else if (cmd == "scheduler-start") handleSchedulerStart();
    else if (cmd == "scheduler-stop")  handleSchedulerStop();
    else if (cmd == "demo")            handleDemo();
    else if (cmd == "screen")          handleScreen(tokens);
    else if (cmd == "report-util")     handleReportUtil();
    else if (cmd == "exit")            { /* handled by run loop */ }
    else                                std::cout << "Unknown command: \"" << cmd << "\".\n";
}

void Console::run() {
    printHeader();
    std::string line;
    while (true) {
        std::cout << "Enter a command: ";
        if (!std::getline(std::cin, line)) break;

        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;
        if (cmd == "exit") { handleExit(); break; }

        dispatch(line);
    }
}
