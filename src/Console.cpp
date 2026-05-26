#include "Console.h"
#include <iostream>
#include <sstream>
#include <cstdlib>

#ifdef _WIN32
#define CLEAR_CMD "cls"
#else
#define CLEAR_CMD "clear"
#endif

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

void Console::dispatch(const std::string& line) {
    std::istringstream iss(line);
    std::string cmd;
    iss >> cmd;

    if (cmd.empty()) return;

    if (cmd == "clear") {
        clearScreen();
        return;
    }

    if (cmd != "initialize" && !initialized_) {
        std::cout << "System not initialized. Run 'initialize' first.\n";
        return;
    }

    if (cmd == "initialize") {
        std::cout << "initialize command recognized. Doing something.\n";
        initialized_ = true;
    } else if (cmd == "screen") {
        std::cout << "screen command recognized. Doing something.\n";
    } else if (cmd == "scheduler-start") {
        std::cout << "scheduler-start command recognized. Doing something.\n";
    } else if (cmd == "scheduler-stop") {
        std::cout << "scheduler-stop command recognized. Doing something.\n";
    } else if (cmd == "report-util") {
        std::cout << "report-util command recognized. Doing something.\n";
    } else {
        std::cout << "Unknown command: \"" << cmd << "\". Type a valid command.\n";
    }
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

        if (cmd == "exit") break;

        dispatch(line);
    }
}
