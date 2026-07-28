if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.14)
    project(cycore_algorithm_sdk LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    option(BUILD_BENCHMARKS "Build SDK frame benchmarks" ON)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endif()

if(BUILD_BENCHMARKS)
    find_path(CYCORE_FLOWGRAPH_INCLUDE_DIR
        NAMES flowgraph/block.h
        HINTS
            "${CMAKE_CURRENT_SOURCE_DIR}/../../../cycore/lib/du/flowgraph/core/include"
    )
    find_path(CYCORE_COMMON_INCLUDE_DIR
        NAMES common/span.h
        HINTS
            "${CMAKE_CURRENT_SOURCE_DIR}/../../../cycore/lib/du/common/include"
    )
    if(NOT CYCORE_FLOWGRAPH_INCLUDE_DIR OR NOT CYCORE_COMMON_INCLUDE_DIR)
        message(FATAL_ERROR
            "Cycore flowgraph/common headers are required for SDK benchmarks")
    endif()
    add_executable(bm_frame_sizes
        test/bm_frame_sizes.cpp
    )
    target_include_directories(bm_frame_sizes PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CYCORE_FLOWGRAPH_INCLUDE_DIR}
        ${CYCORE_COMMON_INCLUDE_DIR}
    )
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(bm_frame_sizes PRIVATE
            $<$<CONFIG:Release>:-O3>
        )
    endif()
endif()
