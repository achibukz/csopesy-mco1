#include "config/Config.h"

#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <unordered_map>

namespace {

uint32_t parseUInt32(const std::string& key, const std::string& value) {
    try {
        size_t idx = 0;
        unsigned long long v = std::stoull(value, &idx);
        if (idx != value.size()) {
            throw ConfigError("config: '" + key + "' has trailing characters in value '" + value + "'");
        }
        if (v > 0xFFFFFFFFULL) {
            throw ConfigError("config: '" + key + "' value " + value + " exceeds uint32 range");
        }
        return static_cast<uint32_t>(v);
    } catch (const std::invalid_argument&) {
        throw ConfigError("config: '" + key + "' expected non-negative integer, got '" + value + "'");
    } catch (const std::out_of_range&) {
        throw ConfigError("config: '" + key + "' value out of range: '" + value + "'");
    }
}

int parseInt(const std::string& key, const std::string& value) {
    try {
        size_t idx = 0;
        long long v = std::stoll(value, &idx);
        if (idx != value.size()) {
            throw ConfigError("config: '" + key + "' has trailing characters in value '" + value + "'");
        }
        if (v > 0x7FFFFFFFLL || v < -0x80000000LL) {
            throw ConfigError("config: '" + key + "' value out of int32 range: '" + value + "'");
        }
        return static_cast<int>(v);
    } catch (const std::invalid_argument&) {
        throw ConfigError("config: '" + key + "' expected integer, got '" + value + "'");
    } catch (const std::out_of_range&) {
        throw ConfigError("config: '" + key + "' value out of range: '" + value + "'");
    }
}

Config::Algo parseAlgo(const std::string& value) {
    if (value == "fcfs") return Config::Algo::FCFS;
    if (value == "rr")   return Config::Algo::RR;
    throw ConfigError("config: 'scheduler' must be 'fcfs' or 'rr', got '" + value + "'");
}

}  // namespace

bool Config::validate(std::string& errOut) const {
    if (numCpu < 1 || numCpu > 128) {
        errOut = "num-cpu must be in [1, 128], got " + std::to_string(numCpu);
        return false;
    }
    if (quantumCycles < 1) {
        errOut = "quantum-cycles must be >= 1";
        return false;
    }
    if (batchProcessFreq < 1) {
        errOut = "batch-process-freq must be >= 1";
        return false;
    }
    if (minIns < 1) {
        errOut = "min-ins must be >= 1";
        return false;
    }
    if (maxIns < minIns) {
        errOut = "max-ins (" + std::to_string(maxIns) +
                 ") must be >= min-ins (" + std::to_string(minIns) + ")";
        return false;
    }
    return true;
}

Config parseConfigFromString(const std::string& body) {
    static const std::set<std::string> kRequired = {
        "num-cpu", "scheduler", "quantum-cycles", "batch-process-freq",
        "min-ins", "max-ins", "delays-per-exec"
    };

    std::unordered_map<std::string, std::string> kv;
    std::istringstream iss(body);
    std::string line;
    int lineNo = 0;

    while (std::getline(iss, line)) {
        ++lineNo;
        std::string trimmed = line;
        auto firstNonSpace = trimmed.find_first_not_of(" \t\r");
        if (firstNonSpace == std::string::npos) continue;
        if (trimmed[firstNonSpace] == '#') continue;

        std::istringstream lineStream(trimmed);
        std::string key, value, extra;
        if (!(lineStream >> key >> value)) {
            throw ConfigError("config: line " + std::to_string(lineNo) +
                              " expected 'key value', got: '" + line + "'");
        }
        if (lineStream >> extra) {
            throw ConfigError("config: line " + std::to_string(lineNo) +
                              " has unexpected extra tokens after value");
        }
        if (!kRequired.count(key)) {
            throw ConfigError("config: unknown key '" + key + "' on line " +
                              std::to_string(lineNo));
        }
        if (kv.count(key)) {
            std::cerr << "config: warning: duplicate key '" << key
                      << "' on line " << lineNo << " (last-wins)\n";
        }
        kv[key] = value;
    }

    for (const auto& req : kRequired) {
        if (!kv.count(req)) {
            throw ConfigError("config: missing required key '" + req + "'");
        }
    }

    Config c;
    c.numCpu           = parseInt("num-cpu", kv["num-cpu"]);
    c.scheduler        = parseAlgo(kv["scheduler"]);
    c.quantumCycles    = parseUInt32("quantum-cycles", kv["quantum-cycles"]);
    c.batchProcessFreq = parseUInt32("batch-process-freq", kv["batch-process-freq"]);
    c.minIns           = parseUInt32("min-ins", kv["min-ins"]);
    c.maxIns           = parseUInt32("max-ins", kv["max-ins"]);
    c.delaysPerExec    = parseUInt32("delays-per-exec", kv["delays-per-exec"]);

    std::string err;
    if (!c.validate(err)) {
        throw ConfigError("config: " + err);
    }
    return c;
}

Config parseConfig(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        throw ConfigError("config: cannot open file '" + path + "'");
    }
    std::ostringstream buf;
    buf << in.rdbuf();
    return parseConfigFromString(buf.str());
}

SchedulerConfig toSchedulerConfig(const Config& full) {
    SchedulerConfig s;
    s.numCpu           = full.numCpu;
    s.algo             = (full.scheduler == Config::Algo::FCFS)
                             ? SchedulerConfig::Algo::FCFS
                             : SchedulerConfig::Algo::RR;
    s.quantum          = full.quantumCycles;
    s.delaysPerExec    = full.delaysPerExec;
    s.batchProcessFreq = full.batchProcessFreq;
    return s;
}
