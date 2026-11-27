#include "cpu_frequency/benchmark.hpp"
#include "cpu_frequency/diagnostics.hpp"

#include <charconv>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {

template <typename Number>
Number Parse(std::string_view value, const char* option) {
    Number parsed{};
    const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size()) {
        throw std::invalid_argument(std::string("invalid value for ") + option);
    }
    return parsed;
}

} // namespace

int main(int argc, char** argv) {
    try {
        cpu_frequency::Config config;
        cpu_frequency::DiagnosticConfig diagnostic_config;
        std::string diagnostic_code;
        bool all_diagnostics = false;
        for (int index = 1; index < argc; ++index) {
            const std::string_view option(argv[index]);
            if (option == "--help" || option == "-h") {
                std::cout << "cpu-frequency [--blocks N] [--warmups N] [--samples N]\n"
                             "cpu-frequency --diagnostic CODE [--arg1 N] [--arg2 N] [--arg3 N]\n"
                             "cpu-frequency --all-diagnostics\n";
                return 0;
            }
            if (option == "--all-diagnostics") {
                all_diagnostics = true;
                continue;
            }
            if (index + 1 >= argc) {
                throw std::invalid_argument("each option requires a value");
            }
            const std::string_view value(argv[++index]);
            if (option == "--blocks") {
                config.blocks_per_sample = Parse<std::uint64_t>(value, "--blocks");
            } else if (option == "--warmups") {
                config.warmups = Parse<std::uint32_t>(value, "--warmups");
            } else if (option == "--samples") {
                config.samples = Parse<std::uint32_t>(value, "--samples");
            } else if (option == "--diagnostic") {
                diagnostic_code = std::string(value);
            } else if (option == "--arg1") {
                diagnostic_config.arg1 = Parse<std::uint64_t>(value, "--arg1");
            } else if (option == "--arg2") {
                diagnostic_config.arg2 = Parse<std::uint64_t>(value, "--arg2");
            } else if (option == "--arg3") {
                diagnostic_config.arg3 = Parse<std::uint64_t>(value, "--arg3");
            } else {
                throw std::invalid_argument(std::string("unknown option: ") + std::string(option));
            }
        }
        if (all_diagnostics && !diagnostic_code.empty()) {
            throw std::invalid_argument("--all-diagnostics and --diagnostic cannot be combined");
        }
        if (all_diagnostics) {
            std::cout << cpu_frequency::ToJson(cpu_frequency::RunAllDiagnostics()) << '\n';
            return 0;
        }
        if (!diagnostic_code.empty()) {
            std::cout << cpu_frequency::ToJson(cpu_frequency::RunDiagnostic(diagnostic_code, diagnostic_config)) << '\n';
            return 0;
        }
        std::cout << cpu_frequency::ToJson(cpu_frequency::Measure(config)) << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "cpu-frequency: " << error.what() << '\n';
        return 2;
    }
}
