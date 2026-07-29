cmake_minimum_required(VERSION 3.16)

project(uestcradar_ringbuf LANGUAGES CXX)

# This target is the single implementation of the shared-memory RingBuffer
# ABI used by both the Sidecar and the worker SDK.
add_library(uestcradar_ringbuf STATIC ringbuf.cpp)
add_library(uestcradar::ringbuf ALIAS uestcradar_ringbuf)

set_target_properties(uestcradar_ringbuf PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

target_compile_features(uestcradar_ringbuf PUBLIC cxx_std_20)
target_include_directories(uestcradar_ringbuf
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>"
)
