cmake_minimum_required(VERSION 3.16)
project(qt5-algorithm-example LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 自动处理 Qt 的 MOC, UIC, RCC
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# 寻找 Qt5 Core 模块
find_package(Qt5 COMPONENTS Core REQUIRED)

# 寻找我们自研的通信基座 SDK
find_package(cycomm_sdk REQUIRED)

# 添加可执行文件
add_executable(qt5_algo
    src/main.cpp
    src/radar_reader.cpp
    src/radar_reader.hpp
    src/channel_processor.cpp
    src/channel_processor.hpp
)

# 链接库
target_link_libraries(qt5_algo PRIVATE
    Qt5::Core
    cycomm::sdk
)
