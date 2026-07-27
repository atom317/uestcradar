cmake_minimum_required(VERSION 3.10)
project(my-algorithm-plugin LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_TESTING "Build frame-Block QA and plugin ABI smoke tests" OFF)
option(BUILD_BENCHMARKS "Build the frame-Block throughput benchmark" OFF)
option(ALGORITHM_ENABLE_NATIVE_OPTIMIZATION
       "Enable -march=native for same-host Release benchmarking" OFF)
option(ALGORITHM_ENABLE_IPO "Enable interprocedural optimization when supported" OFF)

add_library(my_plugin MODULE
    src/algorithm.cpp
    src/algorithm_block.cpp
)

set_target_properties(my_plugin PROPERTIES
    PREFIX ""
    SUFFIX ".so"
)

function(configure_algorithm_target target)
    target_include_directories(${target} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../cpp/sdk/include
        ${CMAKE_CURRENT_SOURCE_DIR}/sdk/include
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE
            $<$<CONFIG:Release>:-O3>
        )
        if(ALGORITHM_ENABLE_NATIVE_OPTIMIZATION AND NOT CMAKE_CROSSCOMPILING)
            target_compile_options(${target} PRIVATE
                $<$<CONFIG:Release>:-march=native>
            )
        endif()
    endif()

    if(ALGORITHM_ENABLE_IPO)
        include(CheckIPOSupported)
        check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)
        if(NOT ipo_supported)
            message(FATAL_ERROR "IPO requested but unsupported: ${ipo_error}")
        endif()
        set_property(TARGET ${target} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    endif()
endfunction()

configure_algorithm_target(my_plugin)

if(BUILD_TESTING)
    enable_testing()

    add_executable(qa_algorithm_block
        src/algorithm.cpp
        test/qa_algorithm_block.cpp
    )
    configure_algorithm_target(qa_algorithm_block)
    add_test(NAME qa_algorithm_block COMMAND qa_algorithm_block)

    add_executable(qa_plugin_load
        test/qa_plugin_load.cpp
    )
    configure_algorithm_target(qa_plugin_load)
    target_link_libraries(qa_plugin_load PRIVATE ${CMAKE_DL_LIBS})
    add_dependencies(qa_plugin_load my_plugin)
    add_test(
        NAME qa_plugin_load
        COMMAND qa_plugin_load $<TARGET_FILE:my_plugin>
    )
endif()

if(BUILD_BENCHMARKS)
    add_executable(bm_algorithm_block
        src/algorithm.cpp
        test/bm_algorithm_block.cpp
    )
    configure_algorithm_target(bm_algorithm_block)
endif()
