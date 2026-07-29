cmake_minimum_required(VERSION 3.16)
project(uestc_pulse_compression LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)
option(BUILD_BENCHMARKS "Build the pulse-compression benchmark" OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(FFTW3F REQUIRED IMPORTED_TARGET fftw3f)

set(PULSE_COMPRESSION_ALGORITHM_SOURCES
    src/pulse_compression_algorithm.cpp
    src/pulse_compression_fft_backend_fftw.cpp
)

function(configure_pulse_compression_target target)
    target_include_directories(${target} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/sdk/include
    )
    target_link_libraries(${target} PRIVATE PkgConfig::FFTW3F)
    target_compile_definitions(${target} PRIVATE
        PULSE_COMPRESSION_NFFT_MODE_POW2=1
        PULSE_COMPRESSION_FFTW_PLAN_ESTIMATE=1
    )
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            $<$<CONFIG:Release>:-O3>
        )
    endif()
endfunction()

add_library(pulse_compression MODULE
    ${PULSE_COMPRESSION_ALGORITHM_SOURCES}
    src/algorithm_block.cpp
)
set_target_properties(pulse_compression PROPERTIES
    PREFIX ""
    SUFFIX ".so"
)
configure_pulse_compression_target(pulse_compression)

if(BUILD_TESTING)
    add_executable(qa_pulse_compression_block
        ${PULSE_COMPRESSION_ALGORITHM_SOURCES}
        test/qa_pulse_compression_block.cpp
    )
    configure_pulse_compression_target(qa_pulse_compression_block)
    add_test(
        NAME qa_pulse_compression_block
        COMMAND qa_pulse_compression_block
    )
endif()

if(BUILD_BENCHMARKS)
    add_executable(bm_pulse_compression_block
        ${PULSE_COMPRESSION_ALGORITHM_SOURCES}
        test/bm_pulse_compression_block.cpp
    )
    configure_pulse_compression_target(bm_pulse_compression_block)
endif()
