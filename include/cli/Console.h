#pragma once

#include <string>
#include <vector>

class Console {
public:
    void run();

    // Promoted to public so tests can drive the REPL dispatcher without
    // spinning up an interactive session. Pure-logic, only stdout side effects.
    void dispatch(const std::string& line);

private:
    bool initialized_ = false;

    void printHeader();
    void clearScreen();

    void handleInitialize();
    void handleSchedulerStart();
    void handleSchedulerStop();
    void handleDemo();
    void handleScreen(const std::vector<std::string>& tokens);
    void handleReportUtil();
    void handleExit();
};
