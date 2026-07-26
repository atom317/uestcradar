cmake_minimum_required(VERSION 3.14)
project(uestc-pulse-compression LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_TESTING "Build algorithm QA and plugin-load tests" OFF)
option(BUILD_BENCHMARKS "Build algorithm throughput benchmarks" OFF)
set(PULSE_COMPRESSION_NFFT_MODE "FAST" CACHE STRING
    "FFTW NFFT policy: FAST, POW2, or EXPLICIT")
set_property(CACHE PULSE_COMPRESSION_NFFT_MODE PROPERTY STRINGS FAST POW2 EXPLICIT)
set(PULSE_COMPRESSION_EXPLICIT_NFFT "0" CACHE STRING
    "NFFT when PULSE_COMPRESSION_NFFT_MODE=EXPLICIT")

find_package(PkgConfig REQUIRED)
pkg_check_modules(FFTW3F REQUIRED IMPORTED_TARGET fftw3f)

set(PULSE_COMPRESSION_SOURCES
    src/algorithm.cpp
    src/pulse_compression_algorithm.cpp
    src/pulse_compression_fft_backend_fftw.cpp
)

function(configure_pulse_compression_target target)
    target_include_directories(${target} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/../sdk/include
    )
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE $<$<CONFIG:Release>:-O3>)
    endif()
    target_link_libraries(${target} PRIVATE PkgConfig::FFTW3F)

    if(PULSE_COMPRESSION_NFFT_MODE STREQUAL "FAST")
        target_compile_definitions(${target} PRIVATE PULSE_COMPRESSION_NFFT_MODE_FAST=1)
    elseif(PULSE_COMPRESSION_NFFT_MODE STREQUAL "POW2")
        target_compile_definitions(${target} PRIVATE PULSE_COMPRESSION_NFFT_MODE_POW2=1)
    elseif(PULSE_COMPRESSION_NFFT_MODE STREQUAL "EXPLICIT")
        if(PULSE_COMPRESSION_EXPLICIT_NFFT STREQUAL "0")
            message(FATAL_ERROR "EXPLICIT NFFT mode requires PULSE_COMPRESSION_EXPLICIT_NFFT")
        endif()
        target_compile_definitions(${target} PRIVATE
            PULSE_COMPRESSION_NFFT_MODE_EXPLICIT=1
            PULSE_COMPRESSION_EXPLICIT_NFFT=${PULSE_COMPRESSION_EXPLICIT_NFFT})
    else()
        message(FATAL_ERROR "Unknown PULSE_COMPRESSION_NFFT_MODE: ${PULSE_COMPRESSION_NFFT_MODE}")
    endif()
endfunction()

add_library(pulse_compression MODULE ${PULSE_COMPRESSION_SOURCES})
set_target_properties(pulse_compression PROPERTIES PREFIX "" SUFFIX ".so")
configure_pulse_compression_target(pulse_compression)

if(BUILD_TESTING)
    enable_testing()

    add_executable(qa_pulse_compression_block
        ${PULSE_COMPRESSION_SOURCES}
        test/qa_pulse_compression_block.cpp
    )
    configure_pulse_compression_target(qa_pulse_compression_block)
    add_test(NAME qa_pulse_compression_block COMMAND qa_pulse_compression_block)

    add_executable(qa_pulse_compression_plugin_load test/qa_plugin_load.cpp)
    configure_pulse_compression_target(qa_pulse_compression_plugin_load)
    target_link_libraries(qa_pulse_compression_plugin_load PRIVATE ${CMAKE_DL_LIBS})
    add_dependencies(qa_pulse_compression_plugin_load pulse_compression)
    add_test(
        NAME qa_pulse_compression_plugin_load
        COMMAND qa_pulse_compression_plugin_load $<TARGET_FILE:pulse_compression>
    )

    add_executable(qa_pulse_compression_peak
        ${PULSE_COMPRESSION_SOURCES}
        test/qa_pulse_compression_peak.cpp
    )
    configure_pulse_compression_target(qa_pulse_compression_peak)
endif()

if(BUILD_BENCHMARKS)
    add_executable(bm_pulse_compression_block
        ${PULSE_COMPRESSION_SOURCES}
        test/bm_pulse_compression_block.cpp
    )
    configure_pulse_compression_target(bm_pulse_compression_block)
endif()
