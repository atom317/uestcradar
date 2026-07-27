if(BUILD_BENCHMARKS)
    add_executable(bm_pod_frame_sizes
        test/bm_pod_frame_sizes.cpp
    )
    target_include_directories(bm_pod_frame_sizes PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(bm_pod_frame_sizes PRIVATE
            $<$<CONFIG:Release>:-O3>
        )
    endif()
endif()
