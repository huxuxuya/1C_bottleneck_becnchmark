#include "cpu_frequency/benchmark.hpp"

#include <iomanip>
#include <locale>
#include <sstream>

namespace cpu_frequency {

std::string ToJson(const Result& result) {
    std::ostringstream json;
    json.imbue(std::locale::classic());
    json << std::fixed << std::setprecision(1);
    json << "{\"schemaVersion\":1"
         << ",\"componentVersion\":\"" << result.component_version << "\""
         << ",\"algorithm\":\"" << result.algorithm << "\""
         << ",\"architecture\":\"" << result.architecture << "\""
         << ",\"compiler\":\"" << CompilerName() << "\""
         << ",\"blocksPerSample\":" << result.blocks_per_sample
         << ",\"warmups\":" << result.warmups
         << ",\"sampleCount\":" << result.sample_count
         << ",\"countedOperationsPerBlock\":" << result.operations_per_block
         << ",\"countedOperationsPerSample\":" << result.operations_per_sample
         << ",\"sampleElapsedNs\":[";
    for (std::size_t index = 0; index < result.elapsed_ns.size(); ++index) {
        if (index != 0) {
            json << ',';
        }
        json << result.elapsed_ns[index];
    }
    json << ']'
         << ",\"sampleChecksums\":[";
    for (std::size_t index = 0; index < result.sample_checksums.size(); ++index) {
        if (index != 0) {
            json << ',';
        }
        json << result.sample_checksums[index];
    }
    json << ']'
         << ",\"minElapsedNs\":" << result.min_elapsed_ns
         << ",\"medianElapsedNs\":" << result.median_elapsed_ns
         << ",\"maxElapsedNs\":" << result.max_elapsed_ns
         << ",\"operationsPerSecond\":" << result.operations_per_second
         << ",\"estimatedMHz\":" << result.estimated_mhz
         << ",\"checksum\":" << result.checksum
         << ",\"physicalClockRead\":false"
         << ",\"interpretation\":\"million counted integer operations per second; MHz-equivalent estimate, not measured clock frequency\"}";
    return json.str();
}

} // namespace cpu_frequency
