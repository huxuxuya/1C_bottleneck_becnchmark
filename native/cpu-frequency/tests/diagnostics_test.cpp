#include "cpu_frequency/diagnostics.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void Require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void CheckDiagnostic(const std::string& code,
                     const cpu_frequency::DiagnosticConfig& config,
                     const std::string& required_metric) {
    const auto result = cpu_frequency::RunDiagnostic(code, config);
    Require(result.probe_code == code, code + " returned a different probe code");
    Require(!result.probe_name.empty(), code + " did not return a probe name");
    Require(!result.primary_metric.empty(), code + " did not declare a primary metric");
    Require(!result.primary_unit.empty(), code + " did not declare a unit");
    Require(!result.details.empty(), code + " did not explain its evidence or limitation");
    Require(result.available || result.primary_value == 0.0, code + " unavailable result must not claim a measurement");
    Require(std::isfinite(result.primary_value), code + " returned a non-finite metric");
    const auto json = cpu_frequency::ToJson(result);
    Require(json.find("\"schemaVersion\":1") != std::string::npos, code + " has no result schema version");
    Require(json.find("\"probeCode\":\"" + code + "\"") != std::string::npos, code + " JSON lost its identity");
    if (result.available) {
        Require(json.find("\"" + required_metric + "\"") != std::string::npos,
                code + " omitted its diagnostic evidence");
    } else {
        Require(json.find("\"available\":false") != std::string::npos,
                code + " did not mark unsupported OS evidence unavailable");
    }
    std::cout << json << '\n';
}

} // namespace

int main() {
    try {
        using cpu_frequency::DiagnosticConfig;
        CheckDiagnostic("CPU_002_INDEPENDENT_INSTRUCTIONS", {20'000U, 3U, 0U}, "fourChainOpsPerSecond");
        CheckDiagnostic("CPU_003_WAKE_LATENCY", {3U, 1U, 0U}, "p95WakeLatenessNs");
        CheckDiagnostic("CPU_004_READY_QUEUE", {1U, 50U, 0U}, "p95ControlResponseNs");
        CheckDiagnostic("CPU_005_CPU_QUOTA", {10U, 0U, 0U}, "throttledUsecDelta");
        CheckDiagnostic("CPU_006_SMT_COMPETITION", {50U, 0U, 0U}, "smtThroughputPercentOfBaseline");
        CheckDiagnostic("CPU_007_CORE_MIGRATION", {50U, 1'000U, 0U}, "migrationCount");
        CheckDiagnostic("CPU_008_IDLE_RECOVERY", {1U, 10'000U, 3U}, "firstAfterIdleNsPerUpdate");
        CheckDiagnostic("CPU_009_SUSTAINED_SPEED", {50U, 10U, 0U}, "medianOpsPerSecond");
        CheckDiagnostic("CPU_010_INTERRUPT_JITTER", {1'000U, 10U, 0U}, "p999BlockDurationNs");
        const auto vcpu_wait = cpu_frequency::RunDiagnostic("CPU_011_VCPU_WAIT", {100U, 1'000U, 0U});
        Require(vcpu_wait.measurements_json.find("\"chunkCount\":100") != std::string::npos,
                "CPU_011 must report the configured number of CPU chunks");
        Require(vcpu_wait.measurements_json.find("\"threadCpuElapsedNs\":") != std::string::npos &&
                    vcpu_wait.measurements_json.find("\"p95SchedulingGapNs\":") != std::string::npos &&
                    vcpu_wait.measurements_json.find("\"systemStealTimeDeltaNs\":") != std::string::npos,
                "CPU_011 must report thread CPU time, scheduling gaps, and steal counter availability");
        if (vcpu_wait.available) {
            Require(vcpu_wait.primary_value > 0.0 && vcpu_wait.primary_value <= 100.0,
                    "CPU_011 effective CPU percentage must be in (0, 100]");
        }
        Require(vcpu_wait.checksum != 0U, "CPU_011 must produce a nonzero work checksum");
        CheckDiagnostic("CPU_011_VCPU_WAIT", {100U, 1'000U, 0U}, "effectiveCpuPercentOfWall");

        const auto all = cpu_frequency::RunAllDiagnostics();
        Require(all.size() == 10U, "RunAllDiagnostics must return all ten probes");
        const auto all_json = cpu_frequency::ToJson(all);
        for (const auto* code : {
                 "CPU_002_INDEPENDENT_INSTRUCTIONS", "CPU_003_WAKE_LATENCY", "CPU_004_READY_QUEUE",
                 "CPU_005_CPU_QUOTA", "CPU_006_SMT_COMPETITION", "CPU_007_CORE_MIGRATION",
                 "CPU_008_IDLE_RECOVERY", "CPU_009_SUSTAINED_SPEED", "CPU_010_INTERRUPT_JITTER",
                 "CPU_011_VCPU_WAIT"}) {
            Require(all_json.find(code) != std::string::npos, std::string("suite missed ") + code);
        }
        std::cout << "PASS: all ten native CPU diagnostics executed and serialized\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
