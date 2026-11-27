#include "cpu_frequency/benchmark.hpp"

#include <algorithm>
#include <chrono>
#include <limits>
#include <stdexcept>

#ifndef CPU_FREQUENCY_VERSION
#define CPU_FREQUENCY_VERSION "0.5.0"
#endif

namespace cpu_frequency {
namespace {

constexpr std::uint64_t kMaxBlocksPerSample = 20'000'000;
constexpr std::uint64_t kMaxTotalBlocks = 15'000'000;
constexpr std::uint32_t kMaxWarmups = 20;
constexpr std::uint32_t kMaxSamples = 31;
constexpr std::uint32_t kCommandsPerBlock = 128;

thread_local volatile std::uint32_t g_checksum_sink = 0;
thread_local volatile std::uint32_t g_runtime_input = 1;

#if defined(_MSC_VER)
#define CPU_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#define CPU_NOINLINE __attribute__((noinline))
#else
#define CPU_NOINLINE
#endif

CPU_NOINLINE std::uint32_t CountOperations(std::uint32_t sum,
                                           const std::uint64_t blocks,
                                           const std::uint32_t value) noexcept {
    // Keep the body aligned with the 7-Zip frequency-probe idea: 64 dependent
    // pairs of add/XOR per outer loop, i.e. 128 counted integer commands.
    // The value comes from a runtime source so the compiler cannot fold the
    // result into a constant. The sink below keeps the final state observable.
#define CPU_STEP() sum += value; sum ^= value
#define CPU_4() CPU_STEP(); CPU_STEP(); CPU_STEP(); CPU_STEP()
#define CPU_16() CPU_4(); CPU_4(); CPU_4(); CPU_4()
#define CPU_64() CPU_16(); CPU_16(); CPU_16(); CPU_16()
    for (std::uint64_t i = 0; i < blocks; ++i) {
        CPU_64();
    }
#undef CPU_64
#undef CPU_16
#undef CPU_4
#undef CPU_STEP
    return sum;
}

std::uint64_t Median(std::vector<std::uint64_t> values) {
    std::sort(values.begin(), values.end());
    const auto middle = values.size() / 2;
    if ((values.size() % 2U) != 0U) {
        return values[middle];
    }
    const auto lower = values[middle - 1];
    const auto upper = values[middle];
    return lower + ((upper - lower) / 2U);
}

std::uint32_t RuntimeSeed(const std::uint32_t sample_number) noexcept {
    const auto mixed = static_cast<std::uint32_t>(sample_number * 0x9E3779B9U);
    g_runtime_input = static_cast<std::uint32_t>(g_runtime_input + mixed + 0x7F4A7C15U);
    return g_runtime_input;
}

} // namespace

Result Measure(const Config& config) {
    if (config.blocks_per_sample == 0 || config.blocks_per_sample > kMaxBlocksPerSample) {
        throw std::invalid_argument("blocks_per_sample must be in the range 1..20000000");
    }
    if (config.warmups > kMaxWarmups) {
        throw std::invalid_argument("warmups must be in the range 0..20");
    }
    if (config.samples < 3 || config.samples > kMaxSamples) {
        throw std::invalid_argument("samples must be in the range 3..31");
    }
    if (config.blocks_per_sample >
        (std::numeric_limits<std::uint64_t>::max() / kCommandsPerBlock)) {
        throw std::invalid_argument("operation count overflows uint64");
    }
    const auto run_count = static_cast<std::uint64_t>(config.warmups) + config.samples;
    if (config.blocks_per_sample > kMaxTotalBlocks / run_count) {
        throw std::invalid_argument("total blocks across warmups and samples exceed 15000000");
    }

    using Clock = std::chrono::steady_clock;
    static_assert(Clock::is_steady, "The benchmark requires a monotonic clock");

    g_runtime_input = 1;
    g_checksum_sink = 0;
    std::uint32_t checksum = 0x6D2B79F5U;
    for (std::uint32_t i = 0; i < config.warmups; ++i) {
        checksum = CountOperations(checksum, config.blocks_per_sample, RuntimeSeed(i));
        g_checksum_sink = checksum;
    }

    Result result;
    result.component_version = CPU_FREQUENCY_VERSION;
    result.algorithm = "dependent_u32_add_xor_7zip_style_v1";
    result.architecture = ArchitectureName();
    result.blocks_per_sample = config.blocks_per_sample;
    result.warmups = config.warmups;
    result.sample_count = config.samples;
    result.operations_per_sample = config.blocks_per_sample * kCommandsPerBlock;
    result.sample_checksums.reserve(config.samples);
    result.elapsed_ns.reserve(config.samples);

    for (std::uint32_t i = 0; i < config.samples; ++i) {
        const auto seed = RuntimeSeed(config.warmups + i);
        const auto start = Clock::now();
        checksum = CountOperations(checksum, config.blocks_per_sample, seed);
        const auto finish = Clock::now();
        g_checksum_sink = checksum;
        result.sample_checksums.push_back(checksum);
        const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(finish - start).count();
        if (elapsed <= 0) {
            throw std::runtime_error("monotonic clock returned a non-positive duration");
        }
        result.elapsed_ns.push_back(static_cast<std::uint64_t>(elapsed));
    }

    result.checksum = checksum;
    const auto extrema = std::minmax_element(result.elapsed_ns.begin(), result.elapsed_ns.end());
    result.min_elapsed_ns = *extrema.first;
    result.median_elapsed_ns = Median(result.elapsed_ns);
    result.max_elapsed_ns = *extrema.second;
    result.operations_per_second = static_cast<double>(result.operations_per_sample) * 1'000'000'000.0 /
                                   static_cast<double>(result.median_elapsed_ns);
    result.estimated_mhz = result.operations_per_second / 1'000'000.0;
    return result;
}

std::string ArchitectureName() {
#if defined(__x86_64__) || defined(_M_X64)
    return "x86_64";
#elif defined(__i386__) || defined(_M_IX86)
    return "x86";
#elif defined(__aarch64__) || defined(_M_ARM64)
    return "arm64";
#elif defined(__arm__) || defined(_M_ARM)
    return "arm";
#elif defined(__e2k__)
    return "e2k";
#else
    return "unknown";
#endif
}

std::string CompilerName() {
#if defined(__clang__)
    return "clang " __clang_version__;
#elif defined(_MSC_VER)
    return "msvc " + std::to_string(_MSC_VER);
#elif defined(__GNUC__)
    return "gcc " __VERSION__;
#else
    return "unknown";
#endif
}

} // namespace cpu_frequency
