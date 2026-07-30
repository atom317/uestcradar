cmake_minimum_required(VERSION 3.16)

project(cycomm_sidecar_core LANGUAGES CXX)

include(CTest)
find_package(Threads REQUIRED)

add_subdirectory(
    ../common/ringbuf
    "${CMAKE_CURRENT_BINARY_DIR}/ringbuf")
add_subdirectory(network)

add_library(cycomm_forwarder STATIC forwarder/forwarder.cpp)
add_library(cycomm::forwarder ALIAS cycomm_forwarder)
target_compile_features(cycomm_forwarder PUBLIC cxx_std_20)
target_include_directories(
    cycomm_forwarder PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(
    cycomm_forwarder
    PUBLIC uestcradar::ringbuf cycomm::network Threads::Threads)
target_compile_options(
    cycomm_forwarder PRIVATE -Wall -Wextra -Wpedantic)

if(BUILD_TESTING)
    add_executable(
        forwarder-test forwarder/forwarder_test.cpp)
    target_link_libraries(
        forwarder-test PRIVATE cycomm::forwarder)
    target_compile_options(
        forwarder-test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME forwarder-test COMMAND forwarder-test)
    set_tests_properties(forwarder-test PROPERTIES TIMEOUT 30)
endif()
