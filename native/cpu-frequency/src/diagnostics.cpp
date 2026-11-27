#include "cpu_frequency/diagnostics.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <processthreadsapi.h>
#include <sysinfoapi.h>
#elif defined(__linux__)
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#endif

#ifndef CPU_FREQUENCY_VERSION
#define CPU_FREQUENCY_VERSION "0.3.0"
#endif

namespace cpu_frequency {
namespace {

using Clock = std::chrono::steady_clock;
constexpr std::uint64_t kMaxWorkUnits = 100'000'000;
constexpr std::uint32_t kMaxSamples = 1'000;
constexpr std::uint32_t kMaxWorkers = 32;

thread_local volatile std::uint64_t g_work_sink = 0;

std::string EscapeJson(const std::string& value) {
    std::ostringstream out;
    for (const char raw : value) {
        const auto ch = static_cast<unsigned char>(raw);
        switch (ch) {
        case '"': out << "\\\""; break;
        case '\\': out << "\\\\"; break;
        case '\b': out << "\\b"; break;
        case '\f': out << "\\f"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (ch < 0x20U) {
                out << "\\u00" << std::hex << std::setw(2) << std::setfill('0')
                    << static_cast<unsigned int>(ch) << std::dec;
            } else {
                out << static_cast<char>(ch);
            }
        }
    }
    return out.str();
}

std::uint64_t Median(std::vector<std::uint64_t> values) {
    if (values.empty()) return 0;
    std::sort(values.begin(), values.end());
    const auto middle = values.size() / 2U;
    if (values.size() % 2U != 0U) return values[middle];
    return values[middle - 1U] + (values[middle] - values[middle - 1U]) / 2U;
}

std::uint64_t Percentile(std::vector<std::uint64_t> values, const double percentile) {
    if (values.empty()) return 0;
    std::sort(values.begin(), values.end());
    const auto rank = static_cast<std::size_t>(std::ceil(percentile * static_cast<double>(values.size())));
    return values[std::max<std::size_t>(1U, rank) - 1U];
}

std::string ArrayJson(const std::vector<std::uint64_t>& values) {
    std::ostringstream out;
    out << '[';
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i != 0) out << ',';
        out << values[i];
    }
    out << ']';
    return out.str();
}

DiagnosticResult MakeResult(std::string code,
                            std::string name,
                            std::string metric,
                            std::string unit,
                            const double value,
                            const std::uint64_t checksum,
                            std::string details,
                            std::string measurements,
                            const bool available = true) {
    return {std::move(code), std::move(name), std::move(metric), std::move(unit),
            std::move(details), std::move(measurements), available, value, checksum};
}

DiagnosticResult Unavailable(std::string code,
                              std::string name,
                              std::string metric,
                              std::string reason) {
    return MakeResult(std::move(code), std::move(name), std::move(metric), "н/д", 0.0, 0,
                      std::move(reason), "{}", false);
}

template <std::size_t Lanes>
std::uint64_t IndependentWork(const std::uint64_t updates_per_lane) noexcept {
    std::array<std::uint32_t, Lanes> state{};
    for (std::size_t i = 0; i < Lanes; ++i) {
        state[i] = static_cast<std::uint32_t>(0x9E3779B9U + static_cast<std::uint32_t>(i * 0x45D9F3BU));
    }
    for (std::uint64_t i = 0; i < updates_per_lane; ++i) {
        for (std::size_t lane = 0; lane < Lanes; ++lane) {
            state[lane] = state[lane] * 1664525U + 1013904223U;
        }
    }
    std::uint64_t checksum = 0;
    for (const auto value : state) checksum = (checksum * 0x100000001B3ULL) ^ value;
    g_work_sink = checksum;
    return checksum;
}

std::uint64_t DependentWork(const std::uint64_t updates) noexcept {
    std::uint64_t state = 0xD1B54A32D192ED03ULL;
    for (std::uint64_t i = 0; i < updates; ++i) {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
    }
    g_work_sink = state;
    return state;
}

template <std::size_t Lanes>
std::pair<double, std::uint64_t> MeasureIndependent(const std::uint64_t total_updates,
                                                    const std::uint32_t samples) {
    const auto updates_per_lane = std::max<std::uint64_t>(1U, total_updates / Lanes);
    const auto counted_updates = updates_per_lane * Lanes;
    std::vector<std::uint64_t> elapsed;
    elapsed.reserve(samples);
    std::uint64_t checksum = 0;
    for (std::uint32_t sample = 0; sample < samples; ++sample) {
        const auto start = Clock::now();
        checksum ^= IndependentWork<Lanes>(updates_per_lane);
        const auto end = Clock::now();
        elapsed.push_back(static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count()));
    }
    const auto median_ns = Median(elapsed);
    const auto operations_per_second = static_cast<double>(counted_updates) * 2.0 * 1'000'000'000.0 /
                                      static_cast<double>(median_ns);
    return {operations_per_second, checksum};
}

DiagnosticResult IndependentInstructions(const DiagnosticConfig& config) {
    const auto requested_updates = config.arg1 == 0 ? 200'000U : config.arg1;
    const auto samples = static_cast<std::uint32_t>(config.arg2 == 0 ? 5U : config.arg2);
    if (requested_updates < 1'000U || requested_updates > kMaxWorkUnits || samples < 3U || samples > 31U) {
        throw std::invalid_argument("CPU_002 expects 1000..100000000 updates and 3..31 samples");
    }
    const auto updates = requested_updates - requested_updates % 8U;
    const auto baseline = MeasureIndependent<1>(updates, samples);
    const auto two = MeasureIndependent<2>(updates, samples);
    const auto four = MeasureIndependent<4>(updates, samples);
    const auto eight = MeasureIndependent<8>(updates, samples);
    const auto best = std::max({two.first, four.first, eight.first});
    const auto speedup = best / baseline.first;
    std::ostringstream metrics;
    metrics.imbue(std::locale::classic());
    metrics << std::setprecision(12)
            << "{\"updatesPerSample\":" << updates
            << ",\"samples\":" << samples
            << ",\"singleChainOpsPerSecond\":" << baseline.first
            << ",\"twoChainOpsPerSecond\":" << two.first
            << ",\"fourChainOpsPerSecond\":" << four.first
            << ",\"eightChainOpsPerSecond\":" << eight.first
            << ",\"bestIndependentSpeedup\":" << speedup
            << ",\"countedOpsPerUpdate\":2} ";
    auto json = metrics.str();
    if (!json.empty() && json.back() == ' ') json.pop_back();
    return MakeResult("CPU_002_INDEPENDENT_INSTRUCTIONS", "002. Независимые инструкции",
                      "fourChainOpsPerSecond", "ops/s", four.first,
                      baseline.second ^ two.second ^ four.second ^ eight.second,
                      "Одинаковое число целочисленных обновлений; сравнение 1/2/4/8 независимых цепочек; PMU cycles не читаются",
                      std::move(json));
}

DiagnosticResult WakeLatency(const DiagnosticConfig& config) {
    const auto samples = static_cast<std::uint32_t>(config.arg1 == 0 ? 25U : config.arg1);
    const auto interval_ms = static_cast<std::uint32_t>(config.arg2 == 0 ? 10U : config.arg2);
    if (samples < 3U || samples > kMaxSamples || interval_ms == 0U || interval_ms > 1'000U ||
        static_cast<std::uint64_t>(samples) * interval_ms > 60'000U) {
        throw std::invalid_argument("CPU_003 expects 3..1000 samples and a total schedule <= 60 seconds");
    }
    std::vector<std::uint64_t> lateness;
    lateness.reserve(samples);
    std::uint64_t checksum = 0;
    std::thread worker([&] {
        auto deadline = Clock::now() + std::chrono::milliseconds(interval_ms * 2U);
        for (std::uint32_t i = 0; i < samples; ++i) {
            std::this_thread::sleep_until(deadline);
            const auto observed = Clock::now();
            const auto late = observed > deadline
                ? std::chrono::duration_cast<std::chrono::nanoseconds>(observed - deadline).count() : 0;
            lateness.push_back(static_cast<std::uint64_t>(late));
            checksum += static_cast<std::uint64_t>(i) ^ lateness.back();
            deadline += std::chrono::milliseconds(interval_ms);
        }
    });
    worker.join();
    const auto p50 = Percentile(lateness, 0.50);
    const auto p95 = Percentile(lateness, 0.95);
    const auto p99 = Percentile(lateness, 0.99);
    std::ostringstream metrics;
    metrics << "{\"samples\":" << samples << ",\"intervalMs\":" << interval_ms
            << ",\"p50WakeLatenessNs\":" << p50
            << ",\"p95WakeLatenessNs\":" << p95
            << ",\"p99WakeLatenessNs\":" << p99
            << ",\"maxWakeLatenessNs\":" << *std::max_element(lateness.begin(), lateness.end())
            << ",\"sampleLatenessNs\":" << ArrayJson(lateness) << '}';
    return MakeResult("CPU_003_WAKE_LATENCY", "003. Задержка пробуждения",
                      "p95WakeLatenessNs", "нс", static_cast<double>(p95), checksum,
                      "Опоздание после sleep_until; таймер и планировщик не разделяются без системной трассы",
                      metrics.str());
}

DiagnosticResult ReadyQueue(const DiagnosticConfig& config) {
    const auto workers = static_cast<std::uint32_t>(config.arg1 == 0 ? 2U : config.arg1);
    const auto duration_ms = static_cast<std::uint32_t>(config.arg2 == 0 ? 250U : config.arg2);
    if (workers == 0U || workers > kMaxWorkers || duration_ms < 50U || duration_ms > 5'000U) {
        throw std::invalid_argument("CPU_004 expects 1..32 workers and 50..5000 ms");
    }
    std::atomic<bool> start{false};
    std::atomic<bool> stop{false};
    std::atomic<std::uint64_t> checksum{0};
    std::atomic<std::uint32_t> ready{0};
    std::vector<std::thread> load_threads;
    load_threads.reserve(workers);
    for (std::uint32_t i = 0; i < workers; ++i) {
        load_threads.emplace_back([&, i] {
            ready.fetch_add(1U, std::memory_order_release);
            while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
            std::uint64_t local = i + 1U;
            while (!stop.load(std::memory_order_relaxed)) local ^= DependentWork(1'000U);
            checksum.fetch_xor(local, std::memory_order_relaxed);
        });
    }
    while (ready.load(std::memory_order_acquire) != workers) std::this_thread::yield();
    std::vector<std::uint64_t> wake_lateness;
    std::vector<std::uint64_t> control_work_ns;
    std::uint64_t control_checksum = 0;
    const auto start_time = Clock::now() + std::chrono::milliseconds(5);
    const auto end_time = start_time + std::chrono::milliseconds(duration_ms);
    start.store(true, std::memory_order_release);
    auto deadline = start_time;
    while (deadline < end_time) {
        std::this_thread::sleep_until(deadline);
        const auto began = Clock::now();
        const auto late = began > deadline
            ? std::chrono::duration_cast<std::chrono::nanoseconds>(began - deadline).count() : 0;
        wake_lateness.push_back(static_cast<std::uint64_t>(late));
        control_checksum ^= DependentWork(4'096U);
        const auto finished = Clock::now();
        control_work_ns.push_back(static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(finished - began).count()));
        deadline += std::chrono::milliseconds(1);
    }
    stop.store(true, std::memory_order_relaxed);
    for (auto& thread : load_threads) thread.join();
    const auto p95_late = Percentile(wake_lateness, 0.95);
    const auto p95_work = Percentile(control_work_ns, 0.95);
    const auto p95 = p95_late + p95_work;
    std::ostringstream metrics;
    metrics << "{\"competingWorkers\":" << workers
            << ",\"durationMs\":" << duration_ms
            << ",\"controlSamples\":" << wake_lateness.size()
            << ",\"p95ScheduledStartLatenessNs\":" << p95_late
            << ",\"p95ControlWorkNs\":" << p95_work
            << ",\"p95ControlResponseNs\":" << p95
            << ",\"p99ControlResponseNs\":" << Percentile(wake_lateness, 0.99) + Percentile(control_work_ns, 0.99)
            << '}';
    return MakeResult("CPU_004_READY_QUEUE", "004. Очередь готовых потоков",
                      "p95ControlResponseNs", "нс", static_cast<double>(p95),
                      checksum.load(std::memory_order_relaxed) ^ control_checksum,
                      "Измерена задержка контрольного потока при заданном числе вычислителей; длина системной run queue не читается",
                      metrics.str());
}

struct QuotaCounters {
    std::string source;
    std::string quota_text;
    double quota_cores = 0.0;
    std::uint64_t period_us = 0;
    std::uint64_t periods = 0;
    std::uint64_t throttled_periods = 0;
    std::uint64_t throttled_us = 0;
    bool unlimited = false;
};

#if defined(__linux__)
std::vector<std::string> Split(const std::string& text, const char separator) {
    std::vector<std::string> parts;
    std::istringstream input(text);
    std::string part;
    while (std::getline(input, part, separator)) parts.push_back(part);
    return parts;
}

std::string DecodeMountPath(std::string value) {
    const std::array<std::pair<const char*, const char*>, 3> escapes{{
        {"\\040", " "}, {"\\011", "\t"}, {"\\134", "\\"}}};
    for (const auto& escape : escapes) {
        std::size_t position = 0;
        while ((position = value.find(escape.first, position)) != std::string::npos) {
            value.replace(position, std::char_traits<char>::length(escape.first), escape.second);
            position += std::char_traits<char>::length(escape.second);
        }
    }
    return value;
}

std::optional<std::filesystem::path> FindCgroupDirectory(const bool version_two) {
    std::ifstream groups("/proc/self/cgroup");
    std::string current_path;
    std::string controllers;
    std::string line;
    while (std::getline(groups, line)) {
        const auto first = line.find(':');
        const auto second = line.find(':', first == std::string::npos ? first : first + 1U);
        if (first == std::string::npos || second == std::string::npos) continue;
        const auto id = line.substr(0, first);
        const auto found_controllers = line.substr(first + 1U, second - first - 1U);
        if ((version_two && id == "0" && found_controllers.empty()) ||
            (!version_two && found_controllers.find("cpu") != std::string::npos)) {
            current_path = line.substr(second + 1U);
            controllers = found_controllers;
            break;
        }
    }
    if (current_path.empty()) return std::nullopt;

    std::ifstream mounts("/proc/self/mountinfo");
    while (std::getline(mounts, line)) {
        const auto separator = line.find(" - ");
        if (separator == std::string::npos) continue;
        const auto left = Split(line.substr(0, separator), ' ');
        const auto right = Split(line.substr(separator + 3U), ' ');
        if (left.size() < 5U || right.size() < 3U) continue;
        const bool correct_fs = version_two ? right[0] == "cgroup2" : right[0] == "cgroup";
        if (!correct_fs) continue;
        if (!version_two && right[2].find("cpu") == std::string::npos &&
            controllers.find("cpu") == std::string::npos) continue;
        const auto root = DecodeMountPath(left[3]);
        const auto mount_point = DecodeMountPath(left[4]);
        auto relative = current_path;
        if (root != "/" && relative.compare(0, root.size(), root) == 0) {
            relative.erase(0, root.size());
        }
        while (!relative.empty() && relative.front() == '/') relative.erase(relative.begin());
        return (std::filesystem::path(mount_point) / relative).lexically_normal();
    }
    return std::nullopt;
}

std::optional<QuotaCounters> ReadLinuxQuota() {
    auto directory = FindCgroupDirectory(true);
    bool v2 = directory.has_value();
    if (!directory) directory = FindCgroupDirectory(false);
    if (!directory) return std::nullopt;
    QuotaCounters result;
    result.source = v2 ? "cgroup_v2" : "cgroup_v1";
    std::ifstream quota_file(*directory / (v2 ? "cpu.max" : "cpu.cfs_quota_us"));
    std::ifstream stats_file(*directory / "cpu.stat");
    if (!quota_file || !stats_file) return std::nullopt;
    if (v2) {
        std::string quota;
        if (!(quota_file >> quota >> result.period_us)) return std::nullopt;
        result.quota_text = quota;
        if (quota == "max") {
            result.unlimited = true;
        } else {
            std::uint64_t quota_us = 0;
            try { quota_us = std::stoull(quota); } catch (...) { return std::nullopt; }
            result.quota_cores = result.period_us == 0U ? 0.0
                : static_cast<double>(quota_us) / static_cast<double>(result.period_us);
        }
    } else {
        long long quota = 0;
        if (!(quota_file >> quota)) return std::nullopt;
        std::ifstream period_file(*directory / "cpu.cfs_period_us");
        if (!(period_file >> result.period_us)) return std::nullopt;
        result.quota_text = quota < 0 ? "max" : std::to_string(quota);
        result.unlimited = quota < 0;
        if (!result.unlimited) {
            result.quota_cores = result.period_us == 0U ? 0.0
                : static_cast<double>(quota) / static_cast<double>(result.period_us);
        }
    }
    std::string key;
    std::uint64_t value = 0;
    while (stats_file >> key >> value) {
        if (key == "nr_periods") result.periods = value;
        else if (key == "nr_throttled") result.throttled_periods = value;
        else if (key == "throttled_usec") result.throttled_us = value;
        else if (key == "throttled_time") result.throttled_us = value / 1'000U;
    }
    return result;
}
#endif

#if defined(__linux__) || defined(_WIN32)
void BurnCpuFor(const std::uint32_t duration_ms, std::uint64_t& checksum) {
    const auto deadline = Clock::now() + std::chrono::milliseconds(duration_ms);
    while (Clock::now() < deadline) checksum ^= DependentWork(10'000U);
}
#endif

DiagnosticResult CpuQuota(const DiagnosticConfig& config) {
    const auto duration_ms = static_cast<std::uint32_t>(config.arg1 == 0 ? 250U : config.arg1);
    if (duration_ms < 10U || duration_ms > 10'000U) {
        throw std::invalid_argument("CPU_005 expects 10..10000 ms");
    }
#if defined(__linux__)
    const auto before = ReadLinuxQuota();
    if (!before) return Unavailable("CPU_005_CPU_QUOTA", "005. Квота CPU", "throttledUsecDelta",
                                    "Не удалось прочитать cpu.max/cpu.stat для cgroup текущего процесса");
    std::uint64_t checksum = 0;
    BurnCpuFor(duration_ms, checksum);
    const auto after = ReadLinuxQuota();
    if (!after) return Unavailable("CPU_005_CPU_QUOTA", "005. Квота CPU", "throttledUsecDelta",
                                   "Счётчики cgroup стали недоступны во время пробы");
    const auto throttled_us = after->throttled_us >= before->throttled_us
        ? after->throttled_us - before->throttled_us : 0U;
    const auto throttled_periods = after->throttled_periods >= before->throttled_periods
        ? after->throttled_periods - before->throttled_periods : 0U;
    const auto periods = after->periods >= before->periods ? after->periods - before->periods : 0U;
    std::ostringstream metrics;
    metrics.imbue(std::locale::classic());
    metrics << std::setprecision(12)
            << "{\"source\":\"" << after->source << "\",\"quota\":\"" << after->quota_text
            << "\",\"unlimited\":" << (after->unlimited ? "true" : "false")
            << ",\"quotaCores\":" << after->quota_cores
            << ",\"periodUs\":" << after->period_us
            << ",\"nrPeriodsDelta\":" << periods
            << ",\"nrThrottledDelta\":" << throttled_periods
            << ",\"throttledUsecDelta\":" << throttled_us
            << ",\"loadWindowMs\":" << duration_ms << '}';
    return MakeResult("CPU_005_CPU_QUOTA", "005. Квота CPU", "throttledUsecDelta", "мкс",
                      static_cast<double>(throttled_us), checksum,
                      after->unlimited ? "Квота cgroup не ограничена; счётчики throttling сняты до/после нагрузки"
                                       : "Снята фактическая квота cgroup и разность throttling-счётчиков до/после нагрузки",
                      metrics.str());
#elif defined(_WIN32)
    JOBOBJECT_CPU_RATE_CONTROL_INFORMATION rate{};
    if (!QueryInformationJobObject(nullptr, JobObjectCpuRateControlInformation,
                                   &rate, sizeof(rate), nullptr)) {
        return Unavailable("CPU_005_CPU_QUOTA", "005. Квота CPU", "configuredCpuRatePercent",
                           "Windows не предоставила CPU-rate текущего Job Object");
    }
    std::uint64_t checksum = 0;
    BurnCpuFor(duration_ms, checksum);
    const bool enabled = (rate.ControlFlags & JOB_OBJECT_CPU_RATE_CONTROL_ENABLE) != 0U;
    const double rate_percent = enabled ? static_cast<double>(rate.CpuRate) / 100.0 : 0.0;
    std::ostringstream metrics;
    metrics.imbue(std::locale::classic());
    metrics << std::setprecision(8) << "{\"source\":\"windows_job_object\",\"rateEnabled\":"
            << (enabled ? "true" : "false") << ",\"configuredCpuRatePercent\":" << rate_percent
            << ",\"controlFlags\":" << rate.ControlFlags
            << ",\"throttlingCountersAvailable\":false,\"loadWindowMs\":" << duration_ms << '}';
    return MakeResult("CPU_005_CPU_QUOTA", "005. Квота CPU", "configuredCpuRatePercent", "%",
                      rate_percent, checksum,
                      enabled ? "Прочитана настройка CPU-rate Job Object; системный счётчик времени throttling не раскрыт этим API"
                              : "Процесс не ограничен CPU-rate текущего Job Object; счётчик throttling не доступен",
                      metrics.str());
#else
    (void)duration_ms;
    return Unavailable("CPU_005_CPU_QUOTA", "005. Квота CPU", "cpuQuota",
                       "Для macOS нет переносимого счётчика cgroup/Job Object; квота не выводится из скорости нагрузки");
#endif
}

struct CpuTarget {
    std::uint32_t id = 0;
#if defined(_WIN32)
    WORD group = 0;
    KAFFINITY mask = 0;
#endif
};

#if defined(__linux__)
bool ParseCpuList(const std::string& text, std::vector<int>& cpus) {
    std::istringstream parts(text);
    std::string part;
    while (std::getline(parts, part, ',')) {
        const auto dash = part.find('-');
        try {
            const int first = std::stoi(part.substr(0, dash));
            const int last = dash == std::string::npos ? first : std::stoi(part.substr(dash + 1U));
            if (first < 0 || last < first || last >= CPU_SETSIZE) return false;
            for (int cpu = first; cpu <= last; ++cpu) cpus.push_back(cpu);
        } catch (...) { return false; }
    }
    return !cpus.empty();
}

std::optional<std::array<CpuTarget, 3>> FindCpuTargets() {
    cpu_set_t allowed;
    CPU_ZERO(&allowed);
    if (sched_getaffinity(0, sizeof(allowed), &allowed) != 0) return std::nullopt;
    std::vector<int> cpus;
    for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
        if (CPU_ISSET(static_cast<std::size_t>(cpu), &allowed)) cpus.push_back(cpu);
    }
    std::optional<std::array<CpuTarget, 3>> targets;
    for (const auto cpu : cpus) {
        std::ifstream siblings_file("/sys/devices/system/cpu/cpu" + std::to_string(cpu) +
                                    "/topology/thread_siblings_list");
        std::string siblings_text;
        std::vector<int> siblings;
        if (!siblings_file || !std::getline(siblings_file, siblings_text) || !ParseCpuList(siblings_text, siblings)) continue;
        int sibling = -1;
        for (const auto item : siblings) {
            if (item != cpu && CPU_ISSET(static_cast<std::size_t>(item), &allowed)) { sibling = item; break; }
        }
        if (sibling < 0) continue;
        std::ifstream package_a_file("/sys/devices/system/cpu/cpu" + std::to_string(cpu) + "/topology/physical_package_id");
        std::ifstream core_a_file("/sys/devices/system/cpu/cpu" + std::to_string(cpu) + "/topology/core_id");
        int package_a = -1;
        int core_a = -1;
        package_a_file >> package_a;
        core_a_file >> core_a;
        int separate = -1;
        for (const auto candidate : cpus) {
            std::ifstream package_file("/sys/devices/system/cpu/cpu" + std::to_string(candidate) + "/topology/physical_package_id");
            std::ifstream core_file("/sys/devices/system/cpu/cpu" + std::to_string(candidate) + "/topology/core_id");
            int package = -1;
            int core = -1;
            package_file >> package;
            core_file >> core;
            if (package != package_a || core != core_a) { separate = candidate; break; }
        }
        if (separate >= 0) {
            targets = std::array<CpuTarget, 3>{{{static_cast<std::uint32_t>(cpu)},
                                                 {static_cast<std::uint32_t>(sibling)},
                                                 {static_cast<std::uint32_t>(separate)}}};
            break;
        }
    }
    return targets;
}
#elif defined(_WIN32)
std::optional<std::array<CpuTarget, 3>> FindCpuTargets() {
    DWORD bytes = 0;
    (void)GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &bytes);
    if (bytes == 0U) return std::nullopt;
    std::vector<unsigned char> buffer(bytes);
    if (!GetLogicalProcessorInformationEx(RelationProcessorCore,
            reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data()), &bytes)) return std::nullopt;
    std::vector<std::vector<CpuTarget>> cores;
    std::size_t offset = 0;
    while (offset + sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX) <= bytes) {
        const auto* info = reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data() + offset);
        if (info->Size == 0U || offset + info->Size > bytes) break;
        if (info->Relationship == RelationProcessorCore) {
            std::vector<CpuTarget> core;
            const auto& relationship = info->Processor;
            for (WORD group_index = 0; group_index < relationship.GroupCount; ++group_index) {
                const auto& group_mask = relationship.GroupMask[group_index];
                for (DWORD bit = 0; bit < sizeof(KAFFINITY) * 8U; ++bit) {
                    const auto mask = static_cast<KAFFINITY>(1) << bit;
                    if ((group_mask.Mask & mask) != 0U) {
                        core.push_back({static_cast<std::uint32_t>(group_mask.Group) * 64U + bit,
                                        group_mask.Group, mask});
                    }
                }
            }
            if (!core.empty()) cores.push_back(std::move(core));
        }
        offset += info->Size;
    }
    for (const auto& core : cores) {
        if (core.size() < 2U) continue;
        for (const auto& other : cores) {
            if (&core != &other && !other.empty()) {
                return std::array<CpuTarget, 3>{{core[0], core[1], other[0]}};
            }
        }
    }
    return std::nullopt;
}
#endif

#if defined(__linux__) || defined(_WIN32)
bool PinCurrentThread(const CpuTarget& target) {
#if defined(__linux__)
    if (target.id >= CPU_SETSIZE) return false;
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(static_cast<std::size_t>(target.id), &set);
    return pthread_setaffinity_np(pthread_self(), sizeof(set), &set) == 0;
#elif defined(_WIN32)
    GROUP_AFFINITY affinity{};
    affinity.Group = target.group;
    affinity.Mask = target.mask;
    return SetThreadGroupAffinity(GetCurrentThread(), &affinity, nullptr) != 0;
#else
    (void)target;
    return false;
#endif
}
#endif

#if defined(__linux__) || defined(_WIN32)
bool CurrentThreadOnTarget(const CpuTarget& target) {
#if defined(__linux__)
    return sched_getcpu() == static_cast<int>(target.id);
#else
    PROCESSOR_NUMBER processor{};
    GetCurrentProcessorNumberEx(&processor);
    const auto actual = static_cast<std::uint32_t>(processor.Group) * 64U + processor.Number;
    return actual == target.id;
#endif
}
#endif

#if defined(__linux__) || defined(_WIN32)
std::pair<double, std::uint64_t> MeasurePinned(const CpuTarget& control_cpu,
                                               const std::optional<CpuTarget>& competing_cpu,
                                               const std::uint32_t duration_ms) {
    std::atomic<bool> go{false};
    std::atomic<bool> pin_ok{true};
    std::atomic<std::uint64_t> competitor_checksum{0};
    std::atomic<bool> competitor_ready{!competing_cpu.has_value()};
    std::thread competitor;
    if (competing_cpu) {
        competitor = std::thread([&] {
            if (!PinCurrentThread(*competing_cpu) || !CurrentThreadOnTarget(*competing_cpu)) {
                pin_ok.store(false, std::memory_order_release);
            }
            competitor_ready.store(true, std::memory_order_release);
            while (!go.load(std::memory_order_acquire)) std::this_thread::yield();
            std::uint64_t local = 0;
            while (go.load(std::memory_order_relaxed)) local ^= DependentWork(10'000U);
            if (!CurrentThreadOnTarget(*competing_cpu)) pin_ok.store(false, std::memory_order_release);
            competitor_checksum.store(local, std::memory_order_relaxed);
        });
    }
    std::uint64_t control_operations = 0;
    std::uint64_t control_elapsed_ns = 0;
    std::uint64_t control_checksum = 0;
    std::thread control([&] {
        if (!PinCurrentThread(control_cpu) || !CurrentThreadOnTarget(control_cpu)) {
            pin_ok.store(false, std::memory_order_release);
        }
        while (!competitor_ready.load(std::memory_order_acquire)) std::this_thread::yield();
        go.store(true, std::memory_order_release);
        const auto start = Clock::now();
        const auto end = start + std::chrono::milliseconds(duration_ms);
        while (Clock::now() < end) {
            control_checksum ^= DependentWork(10'000U);
            control_operations += 20'000U;
        }
        const auto finish = Clock::now();
        control_elapsed_ns = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(finish - start).count());
        if (!CurrentThreadOnTarget(control_cpu)) pin_ok.store(false, std::memory_order_release);
        go.store(false, std::memory_order_release);
    });
    control.join();
    if (competitor.joinable()) competitor.join();
    if (!pin_ok.load(std::memory_order_acquire)) throw std::runtime_error("OS refused CPU affinity for a test thread");
    const auto measured_ns = static_cast<double>(control_elapsed_ns);
    return {measured_ns > 0.0 ? static_cast<double>(control_operations) * 1'000'000'000.0 / measured_ns : 0.0,
            control_checksum ^ competitor_checksum.load(std::memory_order_relaxed)};
}
#endif

DiagnosticResult SmtCompetition(const DiagnosticConfig& config) {
    const auto duration_ms = static_cast<std::uint32_t>(config.arg1 == 0 ? 250U : config.arg1);
    if (duration_ms < 50U || duration_ms > 5'000U) {
        throw std::invalid_argument("CPU_006 expects 50..5000 ms");
    }
#if defined(__linux__) || defined(_WIN32)
    const auto targets = FindCpuTargets();
    if (!targets) return Unavailable("CPU_006_SMT_COMPETITION", "006. Конкуренция SMT", "smtThroughputPercent",
                                    "Не найдена доступная пара SMT-соседей и отдельное физическое ядро");
    try {
        const auto baseline = MeasurePinned((*targets)[0], std::nullopt, duration_ms);
        const auto smt = MeasurePinned((*targets)[0], (*targets)[1], duration_ms);
        const auto separate = MeasurePinned((*targets)[0], (*targets)[2], duration_ms);
        const auto relative = smt.first * 100.0 / baseline.first;
        std::ostringstream metrics;
        metrics.imbue(std::locale::classic());
        metrics << std::setprecision(12) << "{\"controlCpu\":" << (*targets)[0].id
                << ",\"smtSiblingCpu\":" << (*targets)[1].id
                << ",\"separateCoreCpu\":" << (*targets)[2].id
                << ",\"baselineOpsPerSecond\":" << baseline.first
                << ",\"smtNeighborOpsPerSecond\":" << smt.first
                << ",\"separateCoreOpsPerSecond\":" << separate.first
                << ",\"smtThroughputPercentOfBaseline\":" << relative
                << ",\"affinityVerified\":true," << "\"durationMsPerPhase\":" << duration_ms << '}';
        return MakeResult("CPU_006_SMT_COMPETITION", "006. Конкуренция SMT",
                          "smtThroughputPercentOfBaseline", "%", relative,
                          baseline.second ^ smt.second ^ separate.second,
                          "Поток закрепляется на SMT-соседе и отдельном ядре; топология взята из ОС",
                          metrics.str());
    } catch (const std::exception& error) {
        return Unavailable("CPU_006_SMT_COMPETITION", "006. Конкуренция SMT", "smtThroughputPercent",
                           std::string("Нельзя надёжно закрепить потоки: ") + error.what());
    }
#else
    return Unavailable("CPU_006_SMT_COMPETITION", "006. Конкуренция SMT", "smtThroughputPercent",
                       "macOS не предоставляет публичное API точной привязки потока к логическому CPU");
#endif
}

#if defined(__linux__) || defined(_WIN32)
std::optional<std::uint32_t> CurrentCpuId() {
#if defined(__linux__)
    const int cpu = sched_getcpu();
    if (cpu < 0) return std::nullopt;
    return static_cast<std::uint32_t>(cpu);
#elif defined(_WIN32)
    PROCESSOR_NUMBER processor{};
    GetCurrentProcessorNumberEx(&processor);
    return static_cast<std::uint32_t>(processor.Group) * 64U + processor.Number;
#else
    return std::nullopt;
#endif
}
#endif

DiagnosticResult CoreMigration(const DiagnosticConfig& config) {
    const auto duration_ms = static_cast<std::uint32_t>(config.arg1 == 0 ? 250U : config.arg1);
    const auto chunk_ops = config.arg2 == 0 ? 50'000U : config.arg2;
    if (duration_ms < 50U || duration_ms > 10'000U || chunk_ops < 1'000U || chunk_ops > 10'000'000U) {
        throw std::invalid_argument("CPU_007 expects 50..10000 ms and 1000..10000000 updates per chunk");
    }
#if defined(__linux__) || defined(_WIN32)
    std::vector<std::uint64_t> chunk_times;
    std::vector<std::uint64_t> post_migration_times;
    std::uint64_t checksum = 0;
    std::uint64_t migrations = 0;
    auto previous_cpu = CurrentCpuId();
    bool migration_pending = false;
    const auto deadline = Clock::now() + std::chrono::milliseconds(duration_ms);
    while (Clock::now() < deadline) {
        const auto cpu_before = CurrentCpuId();
        bool post_migration_chunk = migration_pending;
        migration_pending = false;
        if (previous_cpu && cpu_before && previous_cpu != cpu_before) {
            ++migrations;
            post_migration_chunk = true;
        }
        const auto start = Clock::now();
        checksum ^= DependentWork(chunk_ops);
        const auto end = Clock::now();
        const auto cpu_after = CurrentCpuId();
        chunk_times.push_back(static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count()));
        if (cpu_before && cpu_after && cpu_before != cpu_after) {
            ++migrations;
            migration_pending = true;
        }
        if (post_migration_chunk) post_migration_times.push_back(chunk_times.back());
        if (cpu_after) previous_cpu = cpu_after;
        else if (cpu_before) previous_cpu = cpu_before;
    }
    if (chunk_times.empty()) return Unavailable("CPU_007_CORE_MIGRATION", "007. Миграция между ядрами", "migrationCount",
                                                  "Не было завершённых порций измерения");
    std::ostringstream metrics;
    metrics << "{\"durationMs\":" << duration_ms << ",\"chunkUpdates\":" << chunk_ops
            << ",\"chunks\":" << chunk_times.size() << ",\"migrationCount\":" << migrations
            << ",\"medianChunkNs\":" << Median(chunk_times)
            << ",\"medianPostMigrationChunkNs\":" << Median(post_migration_times)
            << ",\"cpuIdAvailable\":true," << "\"postMigrationSamples\":" << post_migration_times.size() << '}';
    return MakeResult("CPU_007_CORE_MIGRATION", "007. Миграция между ядрами", "migrationCount", "переходы",
                      static_cast<double>(migrations), checksum,
                      "CPU ID читается для каждой вычислительной порции; влияние миграции оценивается только при наблюдённых переходах",
                      metrics.str());
#else
    return Unavailable("CPU_007_CORE_MIGRATION", "007. Миграция между ядрами", "migrationCount",
                       "macOS не предоставляет публичный API получения текущего CPU ID потока");
#endif
}

std::uint64_t MeasureUpdatesNs(const std::uint64_t updates, std::uint64_t& checksum) {
    const auto start = Clock::now();
    checksum ^= DependentWork(updates);
    const auto end = Clock::now();
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
}

DiagnosticResult IdleRecovery(const DiagnosticConfig& config) {
    const auto idle_ms = static_cast<std::uint32_t>(config.arg1 == 0 ? 100U : config.arg1);
    const auto updates = config.arg2 == 0 ? 200'000U : config.arg2;
    const auto samples = static_cast<std::uint32_t>(config.arg3 == 0 ? 5U : config.arg3);
    if (idle_ms == 0U || idle_ms > 60'000U || updates < 1'000U || updates > kMaxWorkUnits ||
        samples < 3U || samples > 31U) {
        throw std::invalid_argument("CPU_008 expects idle 1..60000 ms, 1000..100000000 updates, and 3..31 samples");
    }
    std::uint64_t checksum = 0;
    (void)MeasureUpdatesNs(updates, checksum);
    (void)MeasureUpdatesNs(updates, checksum);
    std::this_thread::sleep_for(std::chrono::milliseconds(idle_ms));
    const auto first_ns = MeasureUpdatesNs(updates, checksum);
    std::vector<std::uint64_t> warm_ns;
    warm_ns.reserve(samples);
    for (std::uint32_t i = 0; i < samples; ++i) warm_ns.push_back(MeasureUpdatesNs(updates, checksum));
    const auto warm_median = Median(warm_ns);
    const double ratio = warm_median == 0U ? 0.0 : static_cast<double>(first_ns) / static_cast<double>(warm_median);
    std::ostringstream metrics;
    metrics << std::setprecision(12) << "{\"idleMs\":" << idle_ms << ",\"updatesPerSample\":" << updates
            << ",\"firstAfterIdleNsPerUpdate\":" << static_cast<double>(first_ns) / static_cast<double>(updates)
            << ",\"warmMedianNsPerUpdate\":" << static_cast<double>(warm_median) / static_cast<double>(updates)
            << ",\"firstToWarmRatio\":" << ratio
            << ",\"warmSampleElapsedNs\":" << ArrayJson(warm_ns) << '}';
    const auto value = static_cast<double>(first_ns) / static_cast<double>(updates);
    return MakeResult("CPU_008_IDLE_RECOVERY", "008. Восстановление скорости после простоя",
                      "firstAfterIdleNsPerUpdate", "нс/оп", value, checksum,
                      "Сравнение первого одинакового нативного блока после паузы с медианой прогретых блоков; температура и power limit не читаются",
                      metrics.str());
}

DiagnosticResult SustainedSpeed(const DiagnosticConfig& config) {
    const auto duration_ms = static_cast<std::uint32_t>(config.arg1 == 0 ? 3'000U : config.arg1);
    const auto interval_ms = static_cast<std::uint32_t>(config.arg2 == 0 ? 250U : config.arg2);
    if (duration_ms < 50U || duration_ms > 600'000U || interval_ms < 10U || interval_ms > 10'000U ||
        (duration_ms + interval_ms - 1U) / interval_ms > 2'000U) {
        throw std::invalid_argument("CPU_009 expects 50..600000 ms, 10..10000 ms intervals, and <=2000 samples");
    }
    std::vector<std::uint64_t> elapsed_ns;
    std::vector<std::uint64_t> operations;
    std::uint64_t checksum = 0;
    const auto final_deadline = Clock::now() + std::chrono::milliseconds(duration_ms);
    auto sample_start = Clock::now();
    std::uint64_t sample_ops = 0;
    while (sample_start < final_deadline) {
        const auto window_end = std::min(final_deadline, sample_start + std::chrono::milliseconds(interval_ms));
        sample_ops = 0;
        do {
            checksum ^= DependentWork(10'000U);
            sample_ops += 20'000U;
        } while (Clock::now() < window_end);
        const auto sample_end = Clock::now();
        elapsed_ns.push_back(static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(sample_end - sample_start).count()));
        operations.push_back(sample_ops);
        sample_start = sample_end;
    }
    std::vector<double> rates;
    rates.reserve(elapsed_ns.size());
    for (std::size_t i = 0; i < elapsed_ns.size(); ++i) {
        rates.push_back(static_cast<double>(operations[i]) * 1'000'000'000.0 /
                        static_cast<double>(elapsed_ns[i]));
    }
    auto ordered_rates = rates;
    std::sort(ordered_rates.begin(), ordered_rates.end());
    const auto median_rate = ordered_rates[ordered_rates.size() / 2U];
    const auto minmax = std::minmax_element(rates.begin(), rates.end());
    std::ostringstream rate_array;
    rate_array.imbue(std::locale::classic());
    rate_array << std::setprecision(12) << '[';
    for (std::size_t i = 0; i < rates.size(); ++i) {
        if (i != 0) rate_array << ',';
        rate_array << rates[i];
    }
    rate_array << ']';
    std::ostringstream metrics;
    metrics.imbue(std::locale::classic());
    metrics << std::setprecision(12) << "{\"durationMs\":" << duration_ms
            << ",\"intervalMs\":" << interval_ms
            << ",\"windowCount\":" << rates.size()
            << ",\"medianOpsPerSecond\":" << median_rate
            << ",\"minOpsPerSecond\":" << *minmax.first
            << ",\"maxOpsPerSecond\":" << *minmax.second
            << ",\"windowOpsPerSecond\":" << rate_array.str()
            << ",\"temperatureAvailable\":false," << "\"powerLimitAvailable\":false}";
    return MakeResult("CPU_009_SUSTAINED_SPEED", "009. Устойчивая скорость CPU", "medianOpsPerSecond",
                      "ops/s", median_rate, checksum,
                      "Непрерывные одинаковые нативные вычислительные окна; температура и power/thermal limit требуют отдельного источника ОС",
                      metrics.str());
}

} // namespace

DiagnosticResult RunDiagnostic(const std::string& probe_code, const DiagnosticConfig& config) {
    if (probe_code == "CPU_002_INDEPENDENT_INSTRUCTIONS") return IndependentInstructions(config);
    if (probe_code == "CPU_003_WAKE_LATENCY") return WakeLatency(config);
    if (probe_code == "CPU_004_READY_QUEUE") return ReadyQueue(config);
    if (probe_code == "CPU_005_CPU_QUOTA") return CpuQuota(config);
    if (probe_code == "CPU_006_SMT_COMPETITION") return SmtCompetition(config);
    if (probe_code == "CPU_007_CORE_MIGRATION") return CoreMigration(config);
    if (probe_code == "CPU_008_IDLE_RECOVERY") return IdleRecovery(config);
    if (probe_code == "CPU_009_SUSTAINED_SPEED") return SustainedSpeed(config);
    throw std::invalid_argument("unknown CPU native diagnostic code: " + probe_code);
}

std::vector<DiagnosticResult> RunAllDiagnostics() {
    return {
        RunDiagnostic("CPU_002_INDEPENDENT_INSTRUCTIONS", {200'000U, 5U, 0U}),
        RunDiagnostic("CPU_003_WAKE_LATENCY", {25U, 10U, 0U}),
        RunDiagnostic("CPU_004_READY_QUEUE", {2U, 250U, 0U}),
        RunDiagnostic("CPU_005_CPU_QUOTA", {250U, 0U, 0U}),
        RunDiagnostic("CPU_006_SMT_COMPETITION", {250U, 0U, 0U}),
        RunDiagnostic("CPU_007_CORE_MIGRATION", {250U, 50'000U, 0U}),
        RunDiagnostic("CPU_008_IDLE_RECOVERY", {100U, 200'000U, 5U}),
        RunDiagnostic("CPU_009_SUSTAINED_SPEED", {3'000U, 250U, 0U})};
}

std::string ToJson(const DiagnosticResult& result) {
    std::ostringstream json;
    json.imbue(std::locale::classic());
    json << std::setprecision(12)
         << "{\"schemaVersion\":1"
         << ",\"componentVersion\":\"" << CPU_FREQUENCY_VERSION << "\""
         << ",\"probeCode\":\"" << EscapeJson(result.probe_code) << "\""
         << ",\"probeName\":\"" << EscapeJson(result.probe_name) << "\""
         << ",\"available\":" << (result.available ? "true" : "false")
         << ",\"status\":\"" << (result.available ? "Косвенный сигнал" : "Недоступно") << "\""
         << ",\"primaryMetric\":\"" << EscapeJson(result.primary_metric) << "\""
         << ",\"primaryValue\":" << result.primary_value
         << ",\"primaryUnit\":\"" << EscapeJson(result.primary_unit) << "\""
         << ",\"checksum\":" << result.checksum
         << ",\"details\":\"" << EscapeJson(result.details) << "\""
         << ",\"measurements\":" << result.measurements_json << '}';
    return json.str();
}

std::string ToJson(const std::vector<DiagnosticResult>& results) {
    std::ostringstream json;
    json << '[';
    for (std::size_t i = 0; i < results.size(); ++i) {
        if (i != 0) json << ',';
        json << ToJson(results[i]);
    }
    json << ']';
    return json.str();
}

} // namespace cpu_frequency
