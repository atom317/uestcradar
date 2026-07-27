# 1. 模板目录结构

基于 `develop_new_algorithm` 技能开发新算子时，优先基于 [algorithm_template](../algorithm_template) 脚手架进行开发。

```text
algorithm_template/
├── CMakeLists.txt
├── include/data.h              # 算法自定义 InputData/OutputData
├── include/codec.h             # 业务 Payload 编解码契约
├── include/algorithm.h         # 强类型算法接口
├── src/algorithm.cpp           # 算法实现
├── src/algorithm_block.cpp     # 帧级插件导出宏
├── test/                       # 完整帧 QA、插件加载和一键注册 Benchmark
└── sdk/include/                # 只读 Cycore SDK 头文件
```

### 开发者修改范围：
* `include/data.h`
* `include/codec.h`
* `include/algorithm.h`
* `src/algorithm.cpp`
* `src/algorithm_block.cpp`
* `test/qa_algorithm_block.cpp`
* `test/qa_plugin_load.cpp`
* `test/bm_algorithm_block.cpp`
* 必要时修改 `CMakeLists.txt` 链接其他算法依赖库。

> [!IMPORTANT]
> 严禁手动修改模板里的 `sdk/include` 目录内容。

`test/bm_algorithm_block.cpp` 只填写 `CYCORE_REGISTER_BENCHMARK` 的强类型
`InputData` 准备 Lambda，不得复制底层流图驱动代码。
