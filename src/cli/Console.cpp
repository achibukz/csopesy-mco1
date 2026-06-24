#include "cli/Console.h"

#include "config/Config.h"
#include "cli/Demo.h"
#include "cli/Reporter.h"
#include "process/Instructions.h"
#include "scheduler/Scheduler.h"

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
        SchedulerConfig scfg = toSchedulerConfig(cfg);
        Scheduler::instance().initialize(scfg);
        Scheduler::instance().setProcessFactory(
            makeProcessFactory(cfg.minIns, cfg.maxIns));
        initialized_ = true;
        std::cout << "Initialized: " << cfg.numCpu << " cores, "
                  << algoLabel(scfg.algo)
                  << ", quantum=" << cfg.quantumCycles
                  << ", delays-per-exec=" << cfg.delaysPerExec
                  << ", batch-process-freq=" << cfg.batchProcessFreq << ".\n";
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

void Console::handleScreen(const std::vector<std::string>& tokens) {
    screenManager_.handleScreenCommand(tokens, std::cout);
}

void Console::handleReportUtil() {
    Reporter::writeReport("csopesy-log.txt", std::cout);
}

void Console::handleExit() {
    if (screenManager_.isInScreen()) {
        screenManager_.exitScreen(std::cout);
        return;
    }

    if (initialized_) {
        Scheduler::instance().shutdown();
        initialized_ = false;
    }
    exitRequested_ = true;
}

void Console::handleProcessSmi() {
    screenManager_.handleProcessSmi(std::cout);
}

void Console::dispatch(const std::string& line) {
    auto tokens = tokenize(line);
    if (tokens.empty()) return;
    const std::string& cmd = tokens[0];

    if (cmd != "initialize" && cmd != "exit" && !initialized_) {
        std::cout << "System not initialized. Run 'initialize' first.\n";
        return;
    }

    if (screenManager_.isInScreen() && cmd != "process-smi" && cmd != "exit") {
        std::cout << "Unknown command: \"" << cmd << "\".\n";
        return;
    }

    if (cmd == "initialize")           handleInitialize();
    else if (cmd == "exit")            handleExit();
    else if (cmd == "clear")           clearScreen();
    else if (cmd == "scheduler-start") handleSchedulerStart();
    else if (cmd == "scheduler-stop")  handleSchedulerStop();
    else if (cmd == "demo")            handleDemo();
    else if (cmd == "screen")          handleScreen(tokens);
    else if (cmd == "report-util")     handleReportUtil();
    else if (cmd == "process-smi")     handleProcessSmi();
    else                                std::cout << "Unknown command: \"" << cmd << "\".\n";
}

void Console::run() {
    printHeader();
    std::string line;
    while (!exitRequested_) {
        std::cout << "Enter a command: ";
        if (!std::getline(std::cin, line)) break;
        dispatch(line);
    }
}
