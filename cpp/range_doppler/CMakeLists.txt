cmake_minimum_required(VERSION 3.14)
project(uestc-range-doppler LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_TESTING "Build algorithm QA and plugin-load tests" OFF)
option(BUILD_BENCHMARKS "Build algorithm throughput benchmarks" OFF)

function(configure_range_doppler_target target)
    target_include_directories(${target} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../sdk/include
    )
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE $<$<CONFIG:Release>:-O3>)
    endif()
endfunction()

add_library(range_doppler MODULE src/algorithm.cpp)
set_target_properties(range_doppler PROPERTIES PREFIX "" SUFFIX ".so")
configure_range_doppler_target(range_doppler)

if(BUILD_TESTING)
    enable_testing()

    add_executable(qa_range_doppler_block
        src/algorithm.cpp
        test/qa_range_doppler_block.cpp
    )
    configure_range_doppler_target(qa_range_doppler_block)
    add_test(NAME qa_range_doppler_block COMMAND qa_range_doppler_block)

    add_executable(qa_range_doppler_plugin_load test/qa_plugin_load.cpp)
    configure_range_doppler_target(qa_range_doppler_plugin_load)
    target_link_libraries(qa_range_doppler_plugin_load PRIVATE ${CMAKE_DL_LIBS})
    add_dependencies(qa_range_doppler_plugin_load range_doppler)
    add_test(
        NAME qa_range_doppler_plugin_load
        COMMAND qa_range_doppler_plugin_load $<TARGET_FILE:range_doppler>
    )
endif()

if(BUILD_BENCHMARKS)
    add_executable(bm_range_doppler_block
        src/algorithm.cpp
        test/bm_range_doppler_block.cpp
    )
    configure_range_doppler_target(bm_range_doppler_block)
endif()
