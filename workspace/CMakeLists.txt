cmake_minimum_required(VERSION 3.16)

project(uestcradar_worker LANGUAGES CXX)

add_executable(my_algorithm worker.cpp)

find_package(cycomm_sdk REQUIRED)
target_link_libraries(my_algorithm PRIVATE cycomm::sdk)

set_target_properties(my_algorithm PROPERTIES OUTPUT_NAME worker)
