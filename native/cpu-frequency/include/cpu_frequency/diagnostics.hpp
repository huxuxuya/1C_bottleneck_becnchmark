#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace cpu_frequency {

struct DiagnosticConfig {
    // Probe-specific arguments. Values are validated and bounded by RunDiagnostic.
    std::uint64_t arg1 = 0;
    std::uint64_t arg2 = 0;
    std::uint64_t arg3 = 0;
};

struct DiagnosticResult {
    std::string probe_code;
    std::string probe_name;
    std::string primary_metric;
    std::string primary_unit;
    std::string details;
    std::string measurements_json = "{}";
    bool available = false;
    double primary_value = 0.0;
    std::uint64_t checksum = 0;
};

// Codes: CPU_002_INDEPENDENT_INSTRUCTIONS, CPU_003_WAKE_LATENCY,
// CPU_004_READY_QUEUE, CPU_005_CPU_QUOTA, CPU_006_SMT_COMPETITION,
// CPU_007_CORE_MIGRATION, CPU_008_IDLE_RECOVERY, CPU_009_SUSTAINED_SPEED,
// CPU_010_INTERRUPT_JITTER, CPU_011_VCPU_WAIT.
DiagnosticResult RunDiagnostic(const std::string& probe_code,
                               const DiagnosticConfig& config = {});
std::vector<DiagnosticResult> RunAllDiagnostics();
std::string ToJson(const DiagnosticResult& result);
std::string ToJson(const std::vector<DiagnosticResult>& results);

} // namespace cpu_frequency
