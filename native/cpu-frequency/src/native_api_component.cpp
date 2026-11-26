#include "cpu_frequency/benchmark.hpp"

#include "AddInDefBase.h"
#include "ComponentBase.h"
#include "IMemoryManager.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>

#if defined(_WIN32)
#define CPU_API_EXPORT __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#define CPU_API_EXPORT __attribute__((visibility("default")))
#else
#define CPU_API_EXPORT
#endif

namespace {

constexpr WCHAR_T kClassName[] = {'C', 'p', 'u', 'P', 'r', 'o', 'b', 'e', 0};
constexpr char16_t kExtensionName[] = u"CpuProbe";
constexpr char16_t kMethodNameEnglish[] = u"Measure";
constexpr char16_t kMethodNameLocal[] = u"Измерить";
constexpr char16_t kErrorSource[] = u"CpuFrequencyProbe";
constexpr char16_t kInvalidArguments[] = u"Measure expects positive blocks, 0..20 warmups, 3..31 samples, and at most 15000000 total blocks.";
constexpr char16_t kMemoryError[] = u"The 1C memory manager could not allocate the result string.";
constexpr char16_t kRuntimeError[] = u"The native CPU benchmark could not complete.";

static_assert(sizeof(WCHAR_T) == sizeof(char16_t), "1C Native API strings use 16-bit code units");

bool SameName(const WCHAR_T* value, const WCHAR_T* expected) noexcept {
    if (value == nullptr) {
        return false;
    }
    while (*value != 0 && *expected != 0) {
        if (*value != *expected) {
            return false;
        }
        ++value;
        ++expected;
    }
    return *value == 0 && *expected == 0;
}

bool CopyWideText(IMemoryManager* manager,
                  const char16_t* source,
                  const std::size_t code_units,
                  WCHAR_T** destination) noexcept {
    if (manager == nullptr || source == nullptr || destination == nullptr ||
        code_units > (std::numeric_limits<unsigned long>::max() / sizeof(WCHAR_T)) - 1UL) {
        return false;
    }
    void* memory = nullptr;
    const auto bytes = static_cast<unsigned long>((code_units + 1U) * sizeof(WCHAR_T));
    if (!manager->AllocMemory(&memory, bytes) || memory == nullptr) {
        return false;
    }
    std::memcpy(memory, source, code_units * sizeof(WCHAR_T));
    static_cast<WCHAR_T*>(memory)[code_units] = 0;
    *destination = static_cast<WCHAR_T*>(memory);
    return true;
}

bool CopyAsciiText(IMemoryManager* manager,
                   const std::string& source,
                   tVariant* destination) noexcept {
    if (manager == nullptr || destination == nullptr ||
        source.size() > (std::numeric_limits<unsigned long>::max() - 1UL)) {
        return false;
    }
    void* memory = nullptr;
    const auto bytes = static_cast<unsigned long>(source.size() + 1U);
    if (!manager->AllocMemory(&memory, bytes) || memory == nullptr) {
        return false;
    }
    std::memcpy(memory, source.data(), source.size());
    static_cast<char*>(memory)[source.size()] = '\0';
    std::memset(destination, 0, sizeof(*destination));
    destination->pstrVal = static_cast<char*>(memory);
    destination->strLen = static_cast<std::uint32_t>(source.size());
    destination->vt = VTYPE_PSTR;
    return true;
}

bool ToUnsignedInteger(const tVariant& value, std::uint64_t& destination) noexcept {
    const auto type = static_cast<TYPEVAR>(value.vt & VTYPE_TYPEMASK);
    switch (type) {
    case VTYPE_I1:
        if (value.i8Val <= 0) return false;
        destination = static_cast<std::uint64_t>(value.i8Val);
        return true;
    case VTYPE_I2:
        if (value.shortVal <= 0) return false;
        destination = static_cast<std::uint64_t>(value.shortVal);
        return true;
    case VTYPE_I4:
        if (value.lVal <= 0) return false;
        destination = static_cast<std::uint64_t>(value.lVal);
        return true;
    case VTYPE_I8:
        if (value.llVal <= 0) return false;
        destination = static_cast<std::uint64_t>(value.llVal);
        return true;
    case VTYPE_UI1:
        destination = value.ui8Val;
        return destination > 0;
    case VTYPE_UI2:
        destination = value.ushortVal;
        return destination > 0;
    case VTYPE_UI4:
        destination = value.ulVal;
        return destination > 0;
    case VTYPE_UI8:
        destination = value.ullVal;
        return destination > 0;
    case VTYPE_INT:
        if (value.intVal <= 0) return false;
        destination = static_cast<std::uint64_t>(value.intVal);
        return true;
    case VTYPE_UINT:
        destination = value.uintVal;
        return destination > 0;
    case VTYPE_R4: {
        const auto number = static_cast<long double>(value.fltVal);
        if (!std::isfinite(value.fltVal) || number < 1.0L ||
            number > static_cast<long double>(std::numeric_limits<std::uint64_t>::max()) ||
            std::floor(number) != number) return false;
        destination = static_cast<std::uint64_t>(number);
        return true;
    }
    case VTYPE_R8: {
        const auto number = static_cast<long double>(value.dblVal);
        if (!std::isfinite(value.dblVal) || number < 1.0L ||
            number > static_cast<long double>(std::numeric_limits<std::uint64_t>::max()) ||
            std::floor(number) != number) return false;
        destination = static_cast<std::uint64_t>(number);
        return true;
    }
    default:
        return false;
    }
}

class CpuProbe final : public IComponentBase {
public:
    bool ADDIN_API Init(void* interface_pointer) override {
        connection_ = static_cast<IAddInDefBase*>(interface_pointer);
        return connection_ != nullptr;
    }

    bool ADDIN_API setMemManager(void* memory_manager) override {
        memory_manager_ = static_cast<IMemoryManager*>(memory_manager);
        return memory_manager_ != nullptr;
    }

    long ADDIN_API GetInfo() override { return 2000; }

    void ADDIN_API Done() override {
        connection_ = nullptr;
        memory_manager_ = nullptr;
    }

    bool ADDIN_API RegisterExtensionAs(WCHAR_T** extension_name) override {
        return CopyWideText(memory_manager_, kExtensionName,
                            sizeof(kExtensionName) / sizeof(kExtensionName[0]) - 1U,
                            extension_name);
    }

    long ADDIN_API GetNProps() override { return 0; }
    long ADDIN_API FindProp(const WCHAR_T*) override { return -1; }
    const WCHAR_T* ADDIN_API GetPropName(long, long) override { return nullptr; }
    bool ADDIN_API GetPropVal(const long, tVariant*) override { return false; }
    bool ADDIN_API SetPropVal(const long, tVariant*) override { return false; }
    bool ADDIN_API IsPropReadable(const long) override { return false; }
    bool ADDIN_API IsPropWritable(const long) override { return false; }
    long ADDIN_API GetNMethods() override { return 1; }

    long ADDIN_API FindMethod(const WCHAR_T* method_name) override {
        if (method_name == nullptr) {
            return -1;
        }
        constexpr WCHAR_T method_english[] = {'M', 'e', 'a', 's', 'u', 'r', 'e', 0};
        constexpr WCHAR_T method_local[] = {
            static_cast<WCHAR_T>(0x0418), static_cast<WCHAR_T>(0x0437),
            static_cast<WCHAR_T>(0x043C), static_cast<WCHAR_T>(0x0435),
            static_cast<WCHAR_T>(0x0440), static_cast<WCHAR_T>(0x0438),
            static_cast<WCHAR_T>(0x0442), static_cast<WCHAR_T>(0x044C), 0};
        return (SameName(method_name, method_english) || SameName(method_name, method_local)) ? 0 : -1;
    }

    const WCHAR_T* ADDIN_API GetMethodName(const long method_number, const long alias) override {
        if (method_number != 0 || (alias != 0 && alias != 1)) {
            return nullptr;
        }
        WCHAR_T* method_name = nullptr;
        const auto* source = alias == 0 ? kMethodNameEnglish : kMethodNameLocal;
        const auto size = alias == 0
            ? sizeof(kMethodNameEnglish) / sizeof(kMethodNameEnglish[0]) - 1U
            : sizeof(kMethodNameLocal) / sizeof(kMethodNameLocal[0]) - 1U;
        if (!CopyWideText(memory_manager_, source, size, &method_name)) {
            return nullptr;
        }
        return method_name;
    }

    long ADDIN_API GetNParams(const long method_number) override {
        return method_number == 0 ? 3 : 0;
    }

    bool ADDIN_API GetParamDefValue(const long method_number,
                                    const long parameter_number,
                                    tVariant* default_value) override {
        if (method_number != 0 || parameter_number < 0 || parameter_number > 2 || default_value == nullptr) {
            return false;
        }
        std::memset(default_value, 0, sizeof(*default_value));
        if (parameter_number == 0) {
            default_value->llVal = 1'000'000;
            default_value->vt = VTYPE_I8;
        } else {
            default_value->lVal = parameter_number == 1 ? 2 : 7;
            default_value->vt = VTYPE_I4;
        }
        return true;
    }

    bool ADDIN_API HasRetVal(const long method_number) override { return method_number == 0; }
    bool ADDIN_API CallAsProc(const long, tVariant*, const long) override { return false; }

    bool ADDIN_API CallAsFunc(const long method_number,
                              tVariant* return_value,
                              tVariant* parameters,
                              const long parameter_count) override {
        if (method_number != 0 || return_value == nullptr || parameters == nullptr ||
            parameter_count < 1 || parameter_count > 3) {
            ReportError(kInvalidArguments);
            return false;
        }

        try {
            cpu_frequency::Config config;
            std::uint64_t parsed = 0;
            if (!ToUnsignedInteger(parameters[0], parsed)) {
                ReportError(kInvalidArguments);
                return false;
            }
            config.blocks_per_sample = parsed;

            if (parameter_count > 1 && parameters[1].vt != VTYPE_EMPTY) {
                if (!ToUnsignedInteger(parameters[1], parsed) || parsed > 20) {
                    ReportError(kInvalidArguments);
                    return false;
                }
                config.warmups = static_cast<std::uint32_t>(parsed);
            }
            if (parameter_count > 2 && parameters[2].vt != VTYPE_EMPTY) {
                if (!ToUnsignedInteger(parameters[2], parsed) || parsed < 3 || parsed > 31) {
                    ReportError(kInvalidArguments);
                    return false;
                }
                config.samples = static_cast<std::uint32_t>(parsed);
            }

            if (!CopyAsciiText(memory_manager_, cpu_frequency::ToJson(cpu_frequency::Measure(config)), return_value)) {
                ReportError(kMemoryError);
                return false;
            }
            return true;
        } catch (const std::invalid_argument&) {
            ReportError(kInvalidArguments);
            return false;
        } catch (...) {
            ReportError(kRuntimeError);
            return false;
        }
    }

    void ADDIN_API SetLocale(const WCHAR_T*) override {}
    void ADDIN_API SetUserInterfaceLanguageCode(const WCHAR_T*) override {}

private:
    bool ReportError(const char16_t* description) noexcept {
        if (connection_ == nullptr || description == nullptr) {
            return false;
        }
        return connection_->AddError(ADDIN_E_ORDINARY,
                                     reinterpret_cast<const WCHAR_T*>(kErrorSource),
                                     reinterpret_cast<const WCHAR_T*>(description),
                                     0);
    }

    IAddInDefBase* connection_ = nullptr;
    IMemoryManager* memory_manager_ = nullptr;
};

} // namespace

extern "C" CPU_API_EXPORT const WCHAR_T* GetClassNames() {
    return kClassName;
}

extern "C" CPU_API_EXPORT long GetClassObject(const WCHAR_T* class_name, IComponentBase** component) {
    if (component == nullptr || *component != nullptr || !SameName(class_name, kClassName)) {
        return 0;
    }
    try {
        *component = new CpuProbe();
        return 1;
    } catch (...) {
        return 0;
    }
}

extern "C" CPU_API_EXPORT long DestroyObject(IComponentBase** component) {
    if (component == nullptr || *component == nullptr) {
        return 0;
    }
    try {
        delete *component;
        *component = nullptr;
        return 0;
    } catch (...) {
        return -1;
    }
}

extern "C" CPU_API_EXPORT AppCapabilities SetPlatformCapabilities(const AppCapabilities capabilities) {
    return capabilities >= eAppCapabilities1 ? eAppCapabilities1 : eAppCapabilitiesInvalid;
}

extern "C" CPU_API_EXPORT AttachType GetAttachType() {
    return eCanAttachAny;
}
