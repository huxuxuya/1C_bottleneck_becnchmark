#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace cpu_frequency {

struct Config {
    std::uint64_t blocks_per_sample = 1'000'000;
    std::uint32_t warmups = 2;
    std::uint32_t samples = 7;
};

struct Result {
    std::string component_version;
    std::string algorithm;
    std::string architecture;
    std::uint64_t blocks_per_sample = 0;
    std::uint32_t warmups = 0;
    std::uint32_t sample_count = 0;
    std::uint64_t operations_per_block = 128;
    std::uint64_t operations_per_sample = 0;
    std::uint64_t checksum = 0;
    std::vector<std::uint32_t> sample_checksums;
    std::vector<std::uint64_t> elapsed_ns;
    std::uint64_t min_elapsed_ns = 0;
    std::uint64_t median_elapsed_ns = 0;
    std::uint64_t max_elapsed_ns = 0;
    double operations_per_second = 0.0;
    double estimated_mhz = 0.0;
};

// Runs a single-thread, dependent integer add/XOR workload. The MHz value is
// an operation-rate estimate, not a clock-register reading.
Result Measure(const Config& config);

std::string ToJson(const Result& result);
std::string ArchitectureName();
std::string CompilerName();

} // namespace cpu_frequency
