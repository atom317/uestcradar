cmake_minimum_required(VERSION 3.14)
project(uestc-pulse-compression LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_TESTING "Build algorithm QA and plugin-load tests" OFF)
option(BUILD_BENCHMARKS "Build algorithm throughput benchmarks" OFF)

function(configure_pulse_compression_target target)
    target_include_directories(${target} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../sdk/include
    )
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE $<$<CONFIG:Release>:-O3>)
    endif()
endfunction()

add_library(pulse_compression MODULE src/algorithm.cpp)
set_target_properties(pulse_compression PROPERTIES PREFIX "" SUFFIX ".so")
configure_pulse_compression_target(pulse_compression)

if(BUILD_TESTING)
    enable_testing()

    add_executable(qa_pulse_compression_block
        src/algorithm.cpp
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
        src/algorithm.cpp
        test/qa_pulse_compression_peak.cpp
    )
    configure_pulse_compression_target(qa_pulse_compression_peak)
endif()

if(BUILD_BENCHMARKS)
    add_executable(bm_pulse_compression_block
        src/algorithm.cpp
        test/bm_pulse_compression_block.cpp
    )
    configure_pulse_compression_target(bm_pulse_compression_block)
endif()
