#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>

struct Config {
    enum class Algo { FCFS, RR };

    int      numCpu          = 0;
    Algo     scheduler       = Algo::FCFS;
    uint32_t quantumCycles   = 0;
    uint32_t batchProcessFreq = 0;
    uint32_t minIns          = 0;
    uint32_t maxIns          = 0;
    uint32_t delaysPerExec   = 0;

    bool validate(std::string& errOut) const;
};

struct SchedulerConfig {
    int      numCpu          = 0;
    enum class Algo { FCFS, RR } algo = Algo::FCFS;
    uint32_t quantum         = 0;
    uint32_t delaysPerExec   = 0;
    uint32_t batchProcessFreq = 0;
};

class ConfigError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

Config parseConfig(const std::string& path);
Config parseConfigFromString(const std::string& body);

SchedulerConfig toSchedulerConfig(const Config& full);
