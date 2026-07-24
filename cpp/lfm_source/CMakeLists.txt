cmake_minimum_required(VERSION 3.10)
project(cycore-lfm-source-plugin LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 🟢 强指向主项目 cycore 最新的 SDK 头文件以保证 ABIv2 对齐
if(EXISTS "/home/zikun/workspace/cycore")
    set(CYCORE_ROOT "/home/zikun/workspace/cycore")
else()
    set(CYCORE_ROOT "/home/zikun/code/common/cycore")
endif()

include_directories(${CYCORE_ROOT}/lib/du/flowgraph/core/include)
include_directories(${CYCORE_ROOT}/lib/du/flowgraph/blocks/io/include)
include_directories(${CYCORE_ROOT}/lib/du/common/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# 1. 声明编译为 MODULE (动态库)
add_library(cycore_lfm_source_plugin MODULE
    src/lfm_source.cpp
    src/lfm_source_plugin.cpp
)

set_target_properties(cycore_lfm_source_plugin PROPERTIES
    PREFIX ""
    SUFFIX ".so"
)

# 2. 声明编译静态测试可执行文件
add_executable(qa_lfm_source_block
    src/lfm_source.cpp
    test/qa_lfm_source_block.cpp
)
