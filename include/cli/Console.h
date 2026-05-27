#pragma once

#include <string>
#include <vector>

class Console {
public:
    void run();

private:
    bool initialized_ = false;

    void printHeader();
    void clearScreen();
    void dispatch(const std::string& line);

    void handleInitialize();
    void handleSchedulerStart();
    void handleSchedulerStop();
    void handleDemo();
    void handleScreen(const std::vector<std::string>& tokens);
    void handleReportUtil();
    void handleExit();
};
