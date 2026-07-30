cmake_minimum_required(VERSION 3.16)
project(uestcradar_algorithm LANGUAGES CXX)

find_package(cycomm_sdk REQUIRED)

add_executable(algorithm main.cpp)
target_link_libraries(algorithm PRIVATE cycomm::sdk)
