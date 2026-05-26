#pragma once
#include <string>

class Console {
public:
    void run();

private:
    bool initialized_ = false;

    void printHeader();
    void clearScreen();
    void dispatch(const std::string& line);
};
