#pragma once

#include <iosfwd>
#include <string>

class Reporter {
public:
    static std::string buildReport();
    static bool writeReport(const std::string& path, std::ostream& out);
};
