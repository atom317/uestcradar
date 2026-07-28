cmake_minimum_required(VERSION 3.14)
project(uestcradar-file-source LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(BUILD_TESTING "Build FileSource QA" ON)

function(configure_file_source_target target)
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

add_library(file_source MODULE
    src/file_source.cpp
    src/file_source_plugin.cpp
)
set_target_properties(file_source PROPERTIES PREFIX "" SUFFIX ".so")
configure_file_source_target(file_source)

if(BUILD_TESTING)
    enable_testing()
    add_executable(qa_file_source
        src/file_source.cpp
        test/qa_file_source.cpp
    )
    configure_file_source_target(qa_file_source)
    add_test(NAME qa_file_source COMMAND qa_file_source)
endif()
