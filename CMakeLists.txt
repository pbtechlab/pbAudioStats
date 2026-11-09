cmake_minimum_required(VERSION 3.16)
project(pbAudioStats VERSION 1.4.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(PB_TOP_LEVEL_DEFAULT ON)
else()
    set(PB_TOP_LEVEL_DEFAULT OFF)
endif()

option(PB_USE_STATIC_MSVC_RUNTIME "Use the static MSVC runtime for pbAudioStats targets" OFF)

function(pb_apply_msvc_runtime target_name)
    if(MSVC AND PB_USE_STATIC_MSVC_RUNTIME)
        set_property(TARGET ${target_name} PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
endfunction()

# AUD-13: Threads is required by the library; find it before any target so
# consumers using add_subdirectory inherit it.
find_package(Threads REQUIRED)

# ============================================================================
# SIMD / FP compiler flags (applied per-target so the CPU-guard translation
# unit can be compiled WITHOUT AVX2 — it must run on pre-AVX2 CPUs to print
# a diagnostic instead of crashing with an illegal instruction).
#
# /fp:contract (MSVC) and -ffp-contract=fast (GCC/Clang) allow FMA contraction
# in the scalar double hot loops. Contraction performs a*b+c with a single
# rounding, which is MORE accurate than separate multiply+add. Deliberately
# NOT /fp:fast or -ffast-math: those would break the Neumaier compensated
# summation used for long-file loudness accumulation.
# ============================================================================
set(PB_SIMD_FLAGS "")
set(PB_IS_X86 FALSE)
if(MSVC)
    add_compile_options(/MP)      # parallel compilation
    add_compile_options(/utf-8)   # sources are UTF-8 (Japanese comments)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8 AND NOT CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|aarch64")
        set(PB_IS_X86 TRUE)
        list(APPEND PB_SIMD_FLAGS /arch:AVX2)
        if(MSVC_VERSION GREATER_EQUAL 1930)  # VS2022+: /fp:contract
            list(APPEND PB_SIMD_FLAGS /fp:contract)
        endif()
    endif()
else()
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
        set(PB_IS_X86 TRUE)
        list(APPEND PB_SIMD_FLAGS -mavx2 -mfma -ffp-contract=fast)
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|ARM64|arm64")
        # ARM64: NEON is enabled by default
        list(APPEND PB_SIMD_FLAGS -ffp-contract=fast)
    endif()
endif()

# ============================================================================
# Optimization flags and optional LTO (Link Time Optimization)
# ============================================================================
option(PB_ENABLE_IPO
    "Enable interprocedural optimization (may make installed static libraries compiler-specific)"
    OFF)
if(MSVC)
    # MSVC Release optimization
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2 /DNDEBUG")
    # MSVC LTO (Whole Program Optimization) - use with caution for header-only libs
else()
    # GCC/Clang Release optimization
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -DNDEBUG")
endif()

# External headers (dr_libs)
# AUD-13: CMAKE_SOURCE_DIR breaks add_subdirectory consumers; use the current
# source dir plus explicit build/install interface include dirs.
set(EXT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/ext)

# ============================================================================
# Library: pbAudioStatsLib
# ============================================================================

add_library(pbAudioStatsLib STATIC
    lib/src/pbAudioReader.cpp
    lib/src/pbLoudness.cpp
    lib/src/pbTruePeak.cpp
    lib/src/pbRms.cpp
    lib/src/pbAnalyze.cpp
    lib/src/pbNormalize.cpp
)
pb_apply_msvc_runtime(pbAudioStatsLib)

target_include_directories(pbAudioStatsLib PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/lib/include>
    $<INSTALL_INTERFACE:include>
    PRIVATE
    ${EXT_DIR}
)

add_library(pbAudioStats::pbAudioStats ALIAS pbAudioStatsLib)

target_compile_features(pbAudioStatsLib PUBLIC cxx_std_17)

target_link_libraries(pbAudioStatsLib PUBLIC Threads::Threads)

target_compile_options(pbAudioStatsLib PRIVATE ${PB_SIMD_FLAGS})

# Define DR_MP3_IMPLEMENTATION for MP3 support
target_compile_definitions(pbAudioStatsLib PRIVATE DR_MP3_IMPLEMENTATION)

# Explicitly set library output directory
set_target_properties(pbAudioStatsLib PROPERTIES
    EXPORT_NAME pbAudioStats
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
)
if(PB_ENABLE_IPO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT PB_IPO_SUPPORTED OUTPUT PB_IPO_ERROR)
    if(NOT PB_IPO_SUPPORTED)
        message(FATAL_ERROR "PB_ENABLE_IPO=ON but IPO is unavailable: ${PB_IPO_ERROR}")
    endif()
    set_property(TARGET pbAudioStatsLib PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
endif()

# ============================================================================
# CPU guard: compiled WITHOUT AVX2 so it can run anywhere; contains main().
# ============================================================================

# ============================================================================
# CLI Executable: pbAudioStats
# ============================================================================

option(PB_BUILD_CLI "Build the CLI executable" ${PB_TOP_LEVEL_DEFAULT})
if(PB_BUILD_CLI)
    add_library(pbCpuGuard STATIC cli/cpu_check.cpp)
    pb_apply_msvc_runtime(pbCpuGuard)
    if(PB_IS_X86)
        target_compile_definitions(pbCpuGuard PRIVATE PB_REQUIRE_AVX2)
    endif()

    add_executable(pbAudioStats
        cli/main.cpp
    )
    pb_apply_msvc_runtime(pbAudioStats)

    target_compile_options(pbAudioStats PRIVATE ${PB_SIMD_FLAGS})

    target_link_libraries(pbAudioStats PRIVATE pbAudioStatsLib pbCpuGuard)

    target_include_directories(pbAudioStats PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/include
    )

    # Explicitly set CLI output directory
    set_target_properties(pbAudioStats PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/cli
    )

    # Platform-specific settings
    if(APPLE)
        target_compile_definitions(pbAudioStats PRIVATE _DARWIN_C_SOURCE)
    elseif(WIN32)
        target_compile_definitions(pbAudioStats PRIVATE _WIN32_WINNT=0x0601 NOMINMAX)
    endif()
endif()  # PB_BUILD_CLI

# ============================================================================
# Dev tool: per-stage timing micro-benchmark (not built by default)
#   cmake --build . --config Release --target stage_bench
# ============================================================================
# AUD-07: the micro-benchmark lives in local/ (never pushed). Register it only
# when that source actually exists, so a clean Git checkout configures and
# builds without it. When it is present but incomplete, warn instead of failing.
option(PB_BUILD_STAGE_BENCH "Build the per-stage timing micro-benchmark" OFF)
if(PB_BUILD_STAGE_BENCH)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/local/stage_bench.cpp")
        message(FATAL_ERROR
            "PB_BUILD_STAGE_BENCH=ON requires local/stage_bench.cpp")
    endif()
    add_executable(stage_bench EXCLUDE_FROM_ALL local/stage_bench.cpp)
    target_compile_options(stage_bench PRIVATE ${PB_SIMD_FLAGS})
    target_link_libraries(stage_bench PRIVATE pbAudioStatsLib)
    target_include_directories(stage_bench PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/include ${EXT_DIR})
    set_target_properties(stage_bench PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/cli)
endif()

# Public resource benchmark, deliberately separate from correctness tests.
# It is opt-in locally and runs only in the manual benchmark CI workflow.
option(PB_BUILD_BENCHMARKS "Build resource/performance benchmarks" OFF)
if(PB_BUILD_BENCHMARKS)
    add_subdirectory(bench)
endif()

# Install targets
if(PB_BUILD_CLI)
    install(TARGETS pbAudioStats RUNTIME DESTINATION bin)
endif()

install(TARGETS pbAudioStatsLib EXPORT pbAudioStatsTargets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include
)

install(EXPORT pbAudioStatsTargets
    FILE pbAudioStatsTargets.cmake
    NAMESPACE pbAudioStats::
    DESTINATION lib/cmake/pbAudioStats
)

install(FILES lib/include/pbAudioStats.h
    DESTINATION include
)

option(PB_INSTALL_CMAKE_PACKAGE "Install the CMake package config" ON)
if(PB_INSTALL_CMAKE_PACKAGE)
    include(CMakePackageConfigHelpers)

    set(PB_PACKAGE_CONFIG "${CMAKE_CURRENT_BINARY_DIR}/pbAudioStatsConfig.cmake")
    file(WRITE "${PB_PACKAGE_CONFIG}"
"include(CMakeFindDependencyMacro)\nfind_dependency(Threads)\ninclude(\"\${CMAKE_CURRENT_LIST_DIR}/pbAudioStatsTargets.cmake\")\n")

    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/pbAudioStatsConfigVersion.cmake"
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
    )

    install(FILES
        "${PB_PACKAGE_CONFIG}"
        "${CMAKE_CURRENT_BINARY_DIR}/pbAudioStatsConfigVersion.cmake"
        DESTINATION lib/cmake/pbAudioStats
    )
endif()

# AUD-08: public regression test suite. Optional so consumers embedding this
# project do not have to build tests.
option(PB_BUILD_TESTS "Build the public regression test suite" ${PB_TOP_LEVEL_DEFAULT})
if(PB_BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
    enable_testing()
    add_subdirectory(tests)
endif()
