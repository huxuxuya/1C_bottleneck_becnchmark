#include "cpu_frequency/system_info.hpp"

#include "cpu_frequency/benchmark.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <processthreadsapi.h>
#include <sysinfoapi.h>
#include <winreg.h>
#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
#include <intrin.h>
#elif (defined(__i386__) || defined(__x86_64__)) && (defined(__GNUC__) || defined(__clang__))
#include <cpuid.h>
#endif
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <mach/mach_host.h>
#include <mach/vm_statistics.h>
#include <sys/sysctl.h>
#include <sys/utsname.h>
#include <unistd.h>
#elif defined(__linux__)
#include <sched.h>
#include <sys/utsname.h>
#include <unistd.h>
#endif

namespace cpu_frequency {
namespace {

#ifndef CPU_FREQUENCY_VERSION
#define CPU_FREQUENCY_VERSION "0.5.0"
#endif

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

std::string JsonString(const std::string& value) {
    return value.empty() ? "null" : "\"" + EscapeJson(value) + "\"";
}

template <typename Number>
std::string JsonNumber(const Number value, const bool available = true) {
    if (!available) return "null";
    return std::to_string(value);
}

#if defined(__linux__)
std::string Trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1U);
}
#endif

#if defined(_WIN32)
std::string Utf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) return {};
    std::string result(static_cast<std::size_t>(bytes), '\0');
    if (WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
            result.data(), bytes, nullptr, nullptr) != bytes) return {};
    return result;
}

std::string ReadRegistryString(const wchar_t* key_path, const wchar_t* value_name) {
    DWORD bytes = 0;
    const auto flags = RRF_RT_REG_SZ;
    auto result = RegGetValueW(HKEY_LOCAL_MACHINE, key_path, value_name, flags, nullptr, nullptr, &bytes);
    if (result != ERROR_SUCCESS ||
        bytes < sizeof(wchar_t) || bytes > 64U * 1024U) return {};
    std::vector<wchar_t> buffer(bytes / sizeof(wchar_t) + 1U, L'\0');
    result = RegGetValueW(HKEY_LOCAL_MACHINE, key_path, value_name, flags, nullptr, buffer.data(), &bytes);
    if (result != ERROR_SUCCESS) return {};
    return Utf8(std::wstring(buffer.data()));
}

std::string ReadCpuVendor() {
    char vendor[13] = {};
#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
    int registers[4]{};
    __cpuid(registers, 0);
    std::memcpy(vendor, &registers[1], 4U);
    std::memcpy(vendor + 4, &registers[3], 4U);
    std::memcpy(vendor + 8, &registers[2], 4U);
#elif (defined(__i386__) || defined(__x86_64__)) && (defined(__GNUC__) || defined(__clang__))
    unsigned int eax = 0;
    unsigned int ebx = 0;
    unsigned int ecx = 0;
    unsigned int edx = 0;
    if (__get_cpuid(0, &eax, &ebx, &ecx, &edx) != 0) {
        std::memcpy(vendor, &ebx, 4U);
        std::memcpy(vendor + 4, &edx, 4U);
        std::memcpy(vendor + 8, &ecx, 4U);
    }
#endif
    return vendor[0] == '\0' ? std::string{} : std::string(vendor, 12U);
}

std::uint64_t CountProcessorCores() {
    DWORD bytes = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &bytes);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || bytes == 0U) return 0;
    std::vector<unsigned char> buffer(bytes);
    if (!GetLogicalProcessorInformationEx(RelationProcessorCore,
            reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data()), &bytes)) return 0;
    std::uint64_t count = 0;
    std::size_t offset = 0;
    while (offset + sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX) <= bytes) {
        const auto* item = reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data() + offset);
        if (item->Size == 0U || offset + item->Size > bytes) break;
        if (item->Relationship == RelationProcessorCore) ++count;
        offset += item->Size;
    }
    return count;
}

struct WindowsLimits {
    double cpu_rate_percent = 0.0;
    std::uint64_t memory_limit_bytes = 0;
};

WindowsLimits ReadWindowsLimits() {
    WindowsLimits limits;
    JOBOBJECT_CPU_RATE_CONTROL_INFORMATION cpu_rate{};
    if (QueryInformationJobObject(nullptr, JobObjectCpuRateControlInformation,
            &cpu_rate, sizeof(cpu_rate), nullptr) &&
        (cpu_rate.ControlFlags & JOB_OBJECT_CPU_RATE_CONTROL_ENABLE) != 0U &&
        (cpu_rate.ControlFlags & JOB_OBJECT_CPU_RATE_CONTROL_HARD_CAP) != 0U) {
        limits.cpu_rate_percent = static_cast<double>(cpu_rate.CpuRate) / 100.0;
    }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION memory{};
    if (QueryInformationJobObject(nullptr, JobObjectExtendedLimitInformation,
            &memory, sizeof(memory), nullptr)) {
        if ((memory.BasicLimitInformation.LimitFlags & JOB_OBJECT_LIMIT_PROCESS_MEMORY) != 0U) {
            limits.memory_limit_bytes = memory.ProcessMemoryLimit;
        } else if ((memory.BasicLimitInformation.LimitFlags & JOB_OBJECT_LIMIT_JOB_MEMORY) != 0U) {
            limits.memory_limit_bytes = memory.JobMemoryLimit;
        }
    }
    return limits;
}
#endif

#if defined(__APPLE__)
std::string SysctlString(const char* name) {
    std::size_t size = 0;
    if (sysctlbyname(name, nullptr, &size, nullptr, 0) != 0 || size == 0U || size > 64U * 1024U) return {};
    std::string value(size, '\0');
    if (sysctlbyname(name, value.data(), &size, nullptr, 0) != 0) return {};
    if (!value.empty() && value.back() == '\0') value.pop_back();
    return value;
}

std::uint64_t SysctlUInt64(const char* name) {
    std::size_t size = 0;
    if (sysctlbyname(name, nullptr, &size, nullptr, 0) != 0 ||
        (size != sizeof(std::uint32_t) && size != sizeof(std::uint64_t))) return 0;
    if (size == sizeof(std::uint32_t)) {
        std::uint32_t value = 0;
        if (sysctlbyname(name, &value, &size, nullptr, 0) != 0) return 0;
        return value;
    }
    std::uint64_t value = 0;
    if (sysctlbyname(name, &value, &size, nullptr, 0) != 0) return 0;
    return value;
}

std::uint64_t ReadMacAvailableMemory() {
    vm_statistics64_data_t stats{};
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    const auto host = mach_host_self();
    const auto result = host_statistics64(host, HOST_VM_INFO64,
        reinterpret_cast<host_info64_t>(&stats), &count);
    mach_port_deallocate(mach_task_self(), host);
    if (result != KERN_SUCCESS) return 0;
    const long page_size_raw = sysconf(_SC_PAGESIZE);
    if (page_size_raw <= 0) return 0;
    const auto page_size = static_cast<std::uint64_t>(page_size_raw);
    const auto pages = static_cast<std::uint64_t>(stats.free_count) +
                       static_cast<std::uint64_t>(stats.inactive_count) +
                       static_cast<std::uint64_t>(stats.speculative_count);
    return pages <= std::numeric_limits<std::uint64_t>::max() / page_size
        ? pages * page_size : 0U;
}
#endif

#if defined(__linux__)
std::map<std::string, std::string> ReadCpuInfo() {
    std::ifstream input("/proc/cpuinfo");
    std::map<std::string, std::string> fields;
    std::string line;
    while (std::getline(input, line)) {
        const auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        const auto key = Trim(line.substr(0, colon));
        if (key == "model name" || key == "Hardware" || key == "vendor_id" || key == "CPU implementer") {
            const auto value = Trim(line.substr(colon + 1U));
            if (!value.empty()) fields.emplace(key, value);
        }
    }
    return fields;
}

std::string ReadTrimmedFile(const std::filesystem::path& path) {
    std::ifstream input(path);
    std::string value;
    if (!input || !std::getline(input, value)) return {};
    return Trim(value);
}

std::vector<std::string> SplitWhitespace(const std::string& value) {
    std::istringstream input(value);
    std::vector<std::string> fields;
    std::string field;
    while (input >> field) fields.push_back(field);
    return fields;
}

std::string DecodeMountPath(std::string value) {
    const std::array<std::pair<const char*, const char*>, 3> escapes{{
        {"\\040", " "}, {"\\011", "\t"}, {"\\134", "\\"}}};
    for (const auto& escape : escapes) {
        std::size_t position = 0;
        while ((position = value.find(escape.first, position)) != std::string::npos) {
            const auto length = std::char_traits<char>::length(escape.first);
            value.replace(position, length, escape.second);
            position += std::char_traits<char>::length(escape.second);
        }
    }
    return value;
}

std::optional<std::filesystem::path> FindCgroupDirectory(const bool version_two,
                                                         const std::string& controller) {
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
        const bool matches = version_two
            ? id == "0" && found_controllers.empty()
            : found_controllers.find(controller) != std::string::npos;
        if (matches) {
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
        const auto left = SplitWhitespace(line.substr(0, separator));
        const auto right = SplitWhitespace(line.substr(separator + 3U));
        if (left.size() < 5U || right.size() < 3U) continue;
        const bool correct_fs = version_two ? right[0] == "cgroup2" : right[0] == "cgroup";
        if (!correct_fs) continue;
        if (!version_two && right[2].find(controller) == std::string::npos &&
            controllers.find(controller) == std::string::npos) continue;
        const auto root = DecodeMountPath(left[3]);
        const auto mount_point = DecodeMountPath(left[4]);
        auto relative = current_path;
        if (root != "/") {
            if (relative.compare(0, root.size(), root) != 0 ||
                (relative.size() > root.size() && relative[root.size()] != '/')) continue;
            relative.erase(0, root.size());
        }
        while (!relative.empty() && relative.front() == '/') relative.erase(relative.begin());
        return (std::filesystem::path(mount_point) / relative).lexically_normal();
    }
    return std::nullopt;
}

std::string ReadLinuxPrettyName() {
    std::ifstream input("/etc/os-release");
    std::string line;
    while (std::getline(input, line)) {
        if (line.rfind("PRETTY_NAME=", 0) != 0) continue;
        auto value = Trim(line.substr(12U));
        if (value.size() >= 2U && ((value.front() == '"' && value.back() == '"') ||
                                   (value.front() == '\'' && value.back() == '\''))) {
            value = value.substr(1U, value.size() - 2U);
        }
        return value;
    }
    return {};
}

struct LinuxCpuTopology {
    std::uint64_t allowed_logical = 0;
    std::uint64_t physical_cores = 0;
};

LinuxCpuTopology ReadLinuxCpuTopology() {
    LinuxCpuTopology result;
    cpu_set_t allowed;
    CPU_ZERO(&allowed);
    const bool has_affinity = sched_getaffinity(0, sizeof(allowed), &allowed) == 0;
    if (has_affinity) {
        result.allowed_logical = static_cast<std::uint64_t>(CPU_COUNT(&allowed));
    }
    if (result.allowed_logical == 0U) {
        const long online = sysconf(_SC_NPROCESSORS_ONLN);
        if (online > 0) result.allowed_logical = static_cast<std::uint64_t>(online);
    }

    std::set<std::pair<int, int>> cores;
    for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
        if (has_affinity && !CPU_ISSET(static_cast<std::size_t>(cpu), &allowed)) continue;
        std::ifstream package_file("/sys/devices/system/cpu/cpu" + std::to_string(cpu) + "/topology/physical_package_id");
        std::ifstream core_file("/sys/devices/system/cpu/cpu" + std::to_string(cpu) + "/topology/core_id");
        int package = -1;
        int core = -1;
        if (package_file >> package && core_file >> core) cores.emplace(package, core);
    }
    if (!cores.empty()) result.physical_cores = static_cast<std::uint64_t>(cores.size());
    return result;
}

struct LinuxMemory {
    std::uint64_t total_bytes = 0;
    std::uint64_t available_bytes = 0;
};

LinuxMemory ReadLinuxMemory() {
    std::ifstream input("/proc/meminfo");
    std::string key;
    std::uint64_t value_kib = 0;
    std::string unit;
    LinuxMemory result;
    while (input >> key >> value_kib >> unit) {
        if (unit != "kB") continue;
        if (key == "MemTotal:") result.total_bytes = value_kib * 1024U;
        else if (key == "MemAvailable:") result.available_bytes = value_kib * 1024U;
    }
    return result;
}

std::uint64_t ReadLinuxCgroupMemoryLimit() {
    const auto v2_directory = FindCgroupDirectory(true, "memory");
    const auto v1_directory = v2_directory ? std::optional<std::filesystem::path>{}
                                            : FindCgroupDirectory(false, "memory");
    const auto directory = v2_directory ? v2_directory : v1_directory;
    if (!directory) return 0;
    const bool is_v2 = v2_directory.has_value();
    const auto path = *directory / (is_v2 ? "memory.max" : "memory.limit_in_bytes");
    {
        std::ifstream input(path);
        std::string value;
        if (!(input >> value) || value == "max") return 0;
        try {
            const auto limit = std::stoull(value);
            if (limit > 0U && limit < (std::uint64_t{1} << 60U)) return limit;
        } catch (...) {}
    }
    return 0;
}

double ReadLinuxCpuQuotaCores() {
    const auto v2_directory = FindCgroupDirectory(true, "cpu");
    const auto v1_directory = v2_directory ? std::optional<std::filesystem::path>{}
                                            : FindCgroupDirectory(false, "cpu");
    const auto directory = v2_directory ? v2_directory : v1_directory;
    if (!directory) return 0.0;
    const bool is_v2 = v2_directory.has_value();
    std::ifstream quota_file(*directory / (is_v2 ? "cpu.max" : "cpu.cfs_quota_us"));
    std::string quota;
    std::uint64_t period = 0;
    if (is_v2) {
        if (!(quota_file >> quota)) return 0.0;
        if (quota == "max") return 0.0;
        if (!(quota_file >> period) || period == 0U) return 0.0;
        try { return static_cast<double>(std::stoull(quota)) / static_cast<double>(period); }
        catch (...) { return 0.0; }
    }
    std::ifstream v1_period(*directory / "cpu.cfs_period_us");
    long long quota_us = 0;
    std::uint64_t period_us = 0;
    if (quota_file >> quota_us && v1_period >> period_us && quota_us > 0 && period_us > 0U) {
        return static_cast<double>(quota_us) / static_cast<double>(period_us);
    }
    return 0.0;
}
#endif

} // namespace

std::string GetSystemInfoJson() {
    std::string os_name;
    std::string os_version;
    std::string kernel_version;
    std::string machine_model;
    std::string cpu_vendor;
    std::string cpu_model;
    std::string process_limit_source;
    std::uint64_t logical_processors = 0;
    std::uint64_t physical_cores = 0;
    std::uint64_t physical_memory_bytes = 0;
    std::uint64_t available_memory_bytes = 0;
    std::uint64_t process_memory_limit_bytes = 0;
    double cpu_quota_cores = 0.0;
    double job_cpu_rate_percent = 0.0;

#if defined(_WIN32)
    os_name = "Windows";
    os_version = ReadRegistryString(L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"ProductName");
    const auto display_version = ReadRegistryString(L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"DisplayVersion");
    const auto build = ReadRegistryString(L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"CurrentBuildNumber");
    if (!display_version.empty()) os_version += " " + display_version;
    if (!build.empty()) os_version += " (build " + build + ")";
    machine_model = ReadRegistryString(L"HARDWARE\\DESCRIPTION\\System\\BIOS", L"SystemManufacturer");
    const auto product = ReadRegistryString(L"HARDWARE\\DESCRIPTION\\System\\BIOS", L"SystemProductName");
    if (!product.empty()) machine_model += (machine_model.empty() ? "" : " ") + product;
    cpu_model = ReadRegistryString(L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"ProcessorNameString");
    if (cpu_model.empty()) cpu_model = machine_model;
    cpu_vendor = ReadCpuVendor();
    logical_processors = static_cast<std::uint64_t>(GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));
    physical_cores = CountProcessorCores();
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    if (GlobalMemoryStatusEx(&memory)) {
        physical_memory_bytes = memory.ullTotalPhys;
        available_memory_bytes = memory.ullAvailPhys;
    }
    const auto limits = ReadWindowsLimits();
    job_cpu_rate_percent = limits.cpu_rate_percent;
    process_memory_limit_bytes = limits.memory_limit_bytes;
    if (job_cpu_rate_percent > 0.0 || process_memory_limit_bytes > 0U) process_limit_source = "Windows Job Object";
#elif defined(__APPLE__)
    os_name = "macOS";
    os_version = SysctlString("kern.osproductversion");
    kernel_version = SysctlString("kern.osrelease");
    machine_model = SysctlString("hw.model");
    cpu_vendor = SysctlString("machdep.cpu.vendor");
    cpu_model = SysctlString("machdep.cpu.brand_string");
    logical_processors = SysctlUInt64("hw.logicalcpu");
    physical_cores = SysctlUInt64("hw.physicalcpu");
    physical_memory_bytes = SysctlUInt64("hw.memsize");
    available_memory_bytes = ReadMacAvailableMemory();
#elif defined(__linux__)
    struct utsname uname_info{};
    os_name = "Linux";
    if (uname(&uname_info) == 0) kernel_version = uname_info.release;
    os_version = ReadLinuxPrettyName();
    const auto cpu = ReadCpuInfo();
    if (const auto found = cpu.find("vendor_id"); found != cpu.end()) cpu_vendor = found->second;
    else if (const auto found = cpu.find("CPU implementer"); found != cpu.end()) cpu_vendor = found->second;
    if (const auto found = cpu.find("model name"); found != cpu.end()) cpu_model = found->second;
    else if (const auto found = cpu.find("Hardware"); found != cpu.end()) cpu_model = found->second;
    const auto manufacturer = ReadTrimmedFile("/sys/class/dmi/id/sys_vendor");
    const auto product = ReadTrimmedFile("/sys/class/dmi/id/product_name");
    machine_model = manufacturer;
    if (!product.empty()) machine_model += (machine_model.empty() ? "" : " ") + product;
    const auto topology = ReadLinuxCpuTopology();
    logical_processors = topology.allowed_logical;
    physical_cores = topology.physical_cores;
    const auto memory = ReadLinuxMemory();
    physical_memory_bytes = memory.total_bytes;
    available_memory_bytes = memory.available_bytes;
    process_memory_limit_bytes = ReadLinuxCgroupMemoryLimit();
    cpu_quota_cores = ReadLinuxCpuQuotaCores();
    if (process_memory_limit_bytes > 0U || cpu_quota_cores > 0.0) process_limit_source = "cgroup v1/v2";
#else
    os_name = "Unknown";
#endif

    std::ostringstream json;
    json.imbue(std::locale::classic());
    json << "{\"schemaVersion\":1"
         << ",\"componentVersion\":\"" << CPU_FREQUENCY_VERSION << "\""
         << ",\"system\":{\"name\":" << JsonString(os_name)
         << ",\"version\":" << JsonString(os_version)
         << ",\"kernelVersion\":" << JsonString(kernel_version)
         << ",\"architecture\":\"" << ArchitectureName() << "\""
         << ",\"machineModel\":" << JsonString(machine_model) << "}"
         << ",\"cpu\":{\"vendor\":" << JsonString(cpu_vendor)
         << ",\"model\":" << JsonString(cpu_model)
         << ",\"logicalProcessorsReportedByOS\":" << JsonNumber(logical_processors, logical_processors > 0U)
         << ",\"physicalCoresReportedByOS\":" << JsonNumber(physical_cores, physical_cores > 0U)
         << ",\"cgroupQuotaCores\":" << JsonNumber(cpu_quota_cores, cpu_quota_cores > 0.0)
         << ",\"jobCpuRatePercent\":" << JsonNumber(job_cpu_rate_percent, job_cpu_rate_percent > 0.0) << '}'
         << ",\"memory\":{\"physicalTotalBytes\":" << JsonNumber(physical_memory_bytes, physical_memory_bytes > 0U)
         << ",\"physicalAvailableBytes\":" << JsonNumber(available_memory_bytes, available_memory_bytes > 0U)
         << ",\"processLimitBytes\":" << JsonNumber(process_memory_limit_bytes, process_memory_limit_bytes > 0U)
         << ",\"limitSource\":" << JsonString(process_limit_source) << '}'
         << ",\"scope\":\"OS-reported values; cgroup and Job Object limits are included when visible. Virtual machines and containers may expose virtual resources\""
         << '}';
    return json.str();
}

} // namespace cpu_frequency
