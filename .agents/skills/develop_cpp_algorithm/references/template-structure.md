# 1. 模板目录结构

新算子只基于本仓库
[algorithm_template](../algorithm_template) 脚手架开发：

```text
algorithm_template/
├── CMakeLists.txt
├── include/data.h              # POD InputData/OutputData
├── include/algorithm.h         # 强类型算法接口
├── src/algorithm.cpp           # 算法实现
├── src/algorithm_block.cpp     # 帧级插件导出宏
└── test/                       # 算法正确性 QA 和 Benchmark
```

模板不携带 SDK 镜像。所有目标统一包含仓库唯一真源
`uestcradar/cpp/sdk/include`；若该路径不存在，CMake 配置直接失败。

开发者修改：

- `include/data.h`
- `include/algorithm.h`
- `src/algorithm.cpp`
- `src/algorithm_block.cpp`
- `test/qa_algorithm_block.cpp` 中的业务输入和数值断言
- `test/bm_algorithm_block.cpp` 中的代表性 Benchmark 输入
- 必要时修改 CMake 以链接算法依赖库

不存在 `codec.h`，也不得新增业务 Codec。Benchmark 只填写
`CYCORE_REGISTER_BENCHMARK` 的强类型输入准备 Lambda。
