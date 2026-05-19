#include <iostream>
#include <string>
#include <sstream>
#include <cstdlib>

#ifdef _WIN32
#define CLEAR_CMD "cls"
#else
#define CLEAR_CMD "clear"
#endif

void printHeader() {
    std::cout << " _____  _____  ____  _____  ______  _______     __ \n";
    std::cout << "/ ____|/ ____|/ __ \\|  __ \\|  ____|/ ____\\ \\   / /\n";
    std::cout << "| |    | (___ | |  | | |__) | |__  | (___  \\ \\_/ / \n";
    std::cout << "| |     \\___ \\| |  | |  ___/|  __|  \\___ \\  \\   /  \n";
    std::cout << "| |____ ____) | |__| | |    | |____ ____) |  | |   \n";
    std::cout << " \\_____|_____/ \\____/|_|    |______|_____/   |_|   \n";
    std::cout << "\n";
    std::cout << "Hello, Welcome to CSOPESY commandline!\n";
    std::cout << "Type 'exit' to quit, 'clear' to clear the screen\n";
    std::cout << "\n";
    std::cout << "** IMPORTANT: Type 'initialize' to load config and start system **\n";
    std::cout << "\n";
}

int main() {
    printHeader();

    std::string line;
    while (true) {
        std::cout << "Enter a command: ";
        if (!std::getline(std::cin, line)) break;

        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;

        if (cmd == "exit") {
            break;
        } else if (cmd == "clear") {
            std::system(CLEAR_CMD);
            printHeader();
        } else if (cmd == "initialize") {
            std::cout << "initialize command recognized. Doing something.\n";
        } else if (cmd == "screen") {
            std::cout << "screen command recognized. Doing something.\n";
        } else if (cmd == "scheduler-start") {
            std::cout << "scheduler-start command recognized. Doing something.\n";
        } else if (cmd == "scheduler-stop") {
            std::cout << "scheduler-stop command recognized. Doing something.\n";
        } else if (cmd == "report-util") {
            std::cout << "report-util command recognized. Doing something.\n";
        } else if (!cmd.empty()) {
            std::cout << "Unknown command: \"" << cmd << "\". Type a valid command.\n";
        }
    }

    return 0;
}
