#include <gtest/gtest.h>

#include "config/Config.h"

#include <string>

namespace {

std::string fixture(const std::string& name) {
    return std::string(TEST_FIXTURES_DIR) + "/" + name;
}

}  // namespace

TEST(ConfigTest, ValidConfigParsesAllFields) {
    Config c = parseConfig(fixture("config_valid.txt"));
    EXPECT_EQ(c.numCpu, 4);
    EXPECT_EQ(c.scheduler, Config::Algo::RR);
    EXPECT_EQ(c.quantumCycles, 5u);
    EXPECT_EQ(c.batchProcessFreq, 2u);
    EXPECT_EQ(c.minIns, 100u);
    EXPECT_EQ(c.maxIns, 200u);
    EXPECT_EQ(c.delaysPerExec, 0u);

    std::string err;
    EXPECT_TRUE(c.validate(err));
}

TEST(ConfigTest, RejectsOutOfRangeNumCpu) {
    try {
        parseConfig(fixture("config_bad_range.txt"));
        FAIL() << "expected ConfigError";
    } catch (const ConfigError& e) {
        EXPECT_NE(std::string(e.what()).find("num-cpu"), std::string::npos);
    }
}

TEST(ConfigTest, RejectsMissingKey) {
    try {
        parseConfig(fixture("config_missing_key.txt"));
        FAIL() << "expected ConfigError";
    } catch (const ConfigError& e) {
        EXPECT_NE(std::string(e.what()).find("batch-process-freq"), std::string::npos);
    }
}

TEST(ConfigTest, SchedulerFieldFcfsAndRr) {
    std::string fcfs =
        "num-cpu 1\nscheduler fcfs\nquantum-cycles 1\nbatch-process-freq 1\n"
        "min-ins 1\nmax-ins 1\ndelays-per-exec 0\n";
    Config a = parseConfigFromString(fcfs);
    EXPECT_EQ(a.scheduler, Config::Algo::FCFS);

    std::string rr =
        "num-cpu 1\nscheduler rr\nquantum-cycles 1\nbatch-process-freq 1\n"
        "min-ins 1\nmax-ins 1\ndelays-per-exec 0\n";
    Config b = parseConfigFromString(rr);
    EXPECT_EQ(b.scheduler, Config::Algo::RR);
}

TEST(ConfigTest, RejectsUnknownSchedulerValue) {
    std::string sjf =
        "num-cpu 1\nscheduler sjf\nquantum-cycles 1\nbatch-process-freq 1\n"
        "min-ins 1\nmax-ins 1\ndelays-per-exec 0\n";
    EXPECT_THROW(parseConfigFromString(sjf), ConfigError);
}

TEST(ConfigTest, RejectsMinGreaterThanMax) {
    std::string body =
        "num-cpu 1\nscheduler fcfs\nquantum-cycles 1\nbatch-process-freq 1\n"
        "min-ins 100\nmax-ins 50\ndelays-per-exec 0\n";
    EXPECT_THROW(parseConfigFromString(body), ConfigError);
}

TEST(ConfigTest, IgnoresCommentsAndBlankLines) {
    std::string body =
        "# comment\n"
        "\n"
        "num-cpu 2\n"
        "scheduler fcfs\n"
        "# another comment\n"
        "quantum-cycles 1\n"
        "batch-process-freq 1\n"
        "min-ins 1\n"
        "max-ins 2\n"
        "delays-per-exec 0\n";
    Config c = parseConfigFromString(body);
    EXPECT_EQ(c.numCpu, 2);
}

TEST(ConfigTest, RejectsUnknownKey) {
    std::string body =
        "num-cpu 1\nscheduler fcfs\nquantum-cycles 1\nbatch-process-freq 1\n"
        "min-ins 1\nmax-ins 1\ndelays-per-exec 0\nbogus-key 5\n";
    EXPECT_THROW(parseConfigFromString(body), ConfigError);
}

// Parser must accept CRLF line endings (Windows-authored configs).
TEST(ConfigTest, ParsesCrlfLineEndings) {
    const std::string body =
        "num-cpu 4\r\n"
        "scheduler rr\r\n"
        "quantum-cycles 5\r\n"
        "batch-process-freq 1\r\n"
        "min-ins 1\r\n"
        "max-ins 10\r\n"
        "delays-per-exec 0\r\n";
    Config c = parseConfigFromString(body);
    EXPECT_EQ(c.numCpu, 4);
    EXPECT_EQ(c.scheduler, Config::Algo::RR);
    EXPECT_EQ(c.quantumCycles, 5u);
}

// Parser must tolerate trailing spaces and multiple blank lines.
TEST(ConfigTest, TolerantOfBlankLinesAndTrailingSpaces) {
    const std::string body =
        "num-cpu 4   \n"
        "\n"
        "\n"
        "scheduler rr  \n"
        "quantum-cycles 5\n"
        "batch-process-freq 1\n"
        "min-ins 1\n"
        "max-ins 10\n"
        "delays-per-exec 0\n";
    EXPECT_NO_THROW(parseConfigFromString(body));
}

TEST(ConfigTest, ToSchedulerConfigMapsFields) {
    Config full = parseConfig(fixture("config_valid.txt"));
    SchedulerConfig sub = toSchedulerConfig(full);
    EXPECT_EQ(sub.numCpu, full.numCpu);
    EXPECT_EQ(sub.algo, SchedulerConfig::Algo::RR);
    EXPECT_EQ(sub.quantum, full.quantumCycles);
    EXPECT_EQ(sub.delaysPerExec, full.delaysPerExec);
    EXPECT_EQ(sub.batchProcessFreq, full.batchProcessFreq);
}
