#pragma once

#include "scheduler/IProcess.h"

#include <iosfwd>
#include <string>
#include <unordered_map>
#include <vector>

class ScreenManager {
public:
    bool isInScreen() const { return current_ != nullptr; }

    void handleScreenCommand(const std::vector<std::string>& tokens, std::ostream& out);
    void handleProcessSmi(std::ostream& out) const;
    void exitScreen(std::ostream& out);

private:
    void showProcessScreen(IProcess& process, std::ostream& out) const;
    IProcess* findTracked(const std::string& name) const;

    std::unordered_map<std::string, IProcess*> processesByName_;
    IProcess* current_ = nullptr;
};
