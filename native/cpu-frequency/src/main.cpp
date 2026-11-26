#include "cpu_frequency/benchmark.hpp"

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
        for (int index = 1; index < argc; ++index) {
            const std::string_view option(argv[index]);
            if (option == "--help" || option == "-h") {
                std::cout << "cpu-frequency [--blocks N] [--warmups N] [--samples N]\n";
                return 0;
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
            } else {
                throw std::invalid_argument(std::string("unknown option: ") + std::string(option));
            }
        }
        std::cout << cpu_frequency::ToJson(cpu_frequency::Measure(config)) << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "cpu-frequency: " << error.what() << '\n';
        return 2;
    }
}
