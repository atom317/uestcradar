cmake_minimum_required(VERSION 3.14)
project(uestcradar-sim-sink LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(BUILD_TESTING "Build SimSink QA" ON)

function(configure_sim_sink_target target)
    target_include_directories(${target} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/sdk/include
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic
            $<$<CONFIG:Release>:-O3>
        )
    endif()
endfunction()

add_library(sim_sink MODULE
    src/sim_sink.cpp
    src/sim_sink_plugin.cpp
)
set_target_properties(sim_sink PROPERTIES PREFIX "" SUFFIX ".so")
configure_sim_sink_target(sim_sink)

if(BUILD_TESTING)
    enable_testing()
    add_executable(qa_sim_sink
        src/sim_sink.cpp
        test/qa_sim_sink.cpp
    )
    configure_sim_sink_target(qa_sim_sink)
    add_test(NAME qa_sim_sink COMMAND qa_sim_sink)
endif()
