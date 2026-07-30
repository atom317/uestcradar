cmake_minimum_required(VERSION 3.16)
project(uestcradar_signalsource LANGUAGES CXX)

find_package(cycomm_sdk REQUIRED)

add_executable(signalsource main.cpp)
target_link_libraries(signalsource PRIVATE cycomm::sdk)
