cmake_minimum_required(VERSION 3.14)
project(uestcradar-file-source-sim-sink-integration LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(FILE_SOURCE_PLUGIN "" CACHE FILEPATH "Absolute path to file_source.so")
set(SIM_SINK_PLUGIN "" CACHE FILEPATH "Absolute path to sim_sink.so")

if(NOT EXISTS "${FILE_SOURCE_PLUGIN}")
    message(FATAL_ERROR "FILE_SOURCE_PLUGIN must name an existing file_source.so")
endif()
if(NOT EXISTS "${SIM_SINK_PLUGIN}")
    message(FATAL_ERROR "SIM_SINK_PLUGIN must name an existing sim_sink.so")
endif()

enable_testing()
add_executable(qa_file_source_sim_sink
    test/qa_file_source_sim_sink.cpp
)
target_include_directories(qa_file_source_sim_sink PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(qa_file_source_sim_sink PRIVATE ${CMAKE_DL_LIBS})
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(qa_file_source_sim_sink PRIVATE
        -Wall -Wextra -Wpedantic
        $<$<CONFIG:Release>:-O3>
    )
    set_target_properties(
        qa_file_source_sim_sink PROPERTIES ENABLE_EXPORTS ON)
endif()
add_test(
    NAME qa_file_source_sim_sink
    COMMAND qa_file_source_sim_sink
        "${FILE_SOURCE_PLUGIN}"
        "${SIM_SINK_PLUGIN}"
)
