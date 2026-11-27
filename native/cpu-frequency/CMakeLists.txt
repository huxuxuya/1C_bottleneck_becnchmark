cmake_minimum_required(VERSION 3.20)

project(CpuFrequencyProbe VERSION 0.3.0 LANGUAGES CXX)

option(BUILD_NATIVEAPI_COMPONENT "Build the 1C Native API shared library" OFF)
option(BUILD_BENCHMARK_CLI "Build the standalone benchmark runner" ON)
set(ONEC_NATIVEAPI_INCLUDE_DIR "" CACHE PATH "Path to the official 1C Native API include directory")

add_library(cpu_frequency_core STATIC
    src/benchmark.cpp
    src/diagnostics.cpp
    src/json.cpp)
target_include_directories(cpu_frequency_core PUBLIC include)
target_compile_features(cpu_frequency_core PUBLIC cxx_std_17)
target_compile_definitions(cpu_frequency_core PRIVATE CPU_FREQUENCY_VERSION="${PROJECT_VERSION}")
set_target_properties(cpu_frequency_core PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES)

find_package(Threads REQUIRED)
target_link_libraries(cpu_frequency_core PUBLIC Threads::Threads)

if(MSVC)
    target_compile_options(cpu_frequency_core PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(cpu_frequency_core PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

if(BUILD_BENCHMARK_CLI)
    add_executable(cpu-frequency src/main.cpp)
    target_link_libraries(cpu-frequency PRIVATE cpu_frequency_core)
endif()

if(BUILD_NATIVEAPI_COMPONENT)
    if(NOT ONEC_NATIVEAPI_INCLUDE_DIR)
        message(FATAL_ERROR "Set ONEC_NATIVEAPI_INCLUDE_DIR to the include directory from the official 1C VNCOMPS SDK.")
    endif()
    foreach(required_header ComponentBase.h AddInDefBase.h IMemoryManager.h)
        if(NOT EXISTS "${ONEC_NATIVEAPI_INCLUDE_DIR}/${required_header}")
            message(FATAL_ERROR "Missing ${required_header} in ONEC_NATIVEAPI_INCLUDE_DIR=${ONEC_NATIVEAPI_INCLUDE_DIR}")
        endif()
    endforeach()

    add_library(cpu_frequency_addin SHARED src/native_api_component.cpp)
    target_include_directories(cpu_frequency_addin PRIVATE
        "${ONEC_NATIVEAPI_INCLUDE_DIR}"
        include)
    target_link_libraries(cpu_frequency_addin PRIVATE cpu_frequency_core)
    target_compile_features(cpu_frequency_addin PRIVATE cxx_std_17)
    target_compile_definitions(cpu_frequency_addin PRIVATE CPU_FREQUENCY_ADDIN_BUILD=1)
    set_target_properties(cpu_frequency_addin PROPERTIES
        OUTPUT_NAME CpuFrequencyProbe
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES)

    if(WIN32)
        target_sources(cpu_frequency_addin PRIVATE src/native_api.def)
        target_compile_definitions(cpu_frequency_addin PRIVATE _WINDOWS)
        set_target_properties(cpu_frequency_addin PROPERTIES PREFIX "" SUFFIX ".dll")
        if(MSVC)
            set_property(TARGET cpu_frequency_addin PROPERTY MSVC_RUNTIME_LIBRARY
                "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        elseif(MINGW)
            target_link_options(cpu_frequency_addin PRIVATE -static -static-libgcc -static-libstdc++)
        endif()
    elseif(APPLE)
        set_target_properties(cpu_frequency_addin PROPERTIES PREFIX "" SUFFIX ".dylib")
    else()
        set_target_properties(cpu_frequency_addin PROPERTIES PREFIX "" SUFFIX ".so")
        target_link_options(cpu_frequency_addin PRIVATE -Wl,--exclude-libs,ALL)
        if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            target_link_options(cpu_frequency_addin PRIVATE -static-libgcc -static-libstdc++)
        endif()
    endif()
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(cpu_frequency_diagnostics_test tests/diagnostics_test.cpp)
    target_link_libraries(cpu_frequency_diagnostics_test PRIVATE cpu_frequency_core)
    add_test(NAME cpu_frequency_diagnostics COMMAND cpu_frequency_diagnostics_test)
endif()
