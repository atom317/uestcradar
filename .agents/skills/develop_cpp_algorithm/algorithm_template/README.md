stru

# C++ 算法开发指南

本指南用于在 `uestcradar` 仓库内开发帧级算法插件。开发者定义强类型完整帧
数据、实现数学逻辑并填写 QA/Benchmark 输入；SDK 自动完成拆帧、变长帧编解码、
元数据透传和输出封包。

## 目录与修改范围

```text
my_algorithm/
├── CMakeLists.txt
├── Dockerfile.build_cross
├── include/
│   ├── data.h                  # 强类型 InputData/OutputData
│   └── algorithm.h             # 算法接口
├── src/
│   ├── algorithm.cpp           # 数学实现
│   └── algorithm_block.cpp     # 插件注册
└── test/
    ├── qa_algorithm_block.cpp  # 算法功能与数学正确性
    └── bm_algorithm_block.cpp  # 一键式性能基准
```

模板携带 `sdk/include` 稳定 SDK 快照，可脱离 `uestcradar` 仓库独立配置和编译。
开发者不得修改该目录；SDK 更新由基础设施维护者统一完成。

开发者只需修改 `data.h`、算法头文件/实现、插件标识和两个测试的业务输入/断言。
不存在 `codec.h`，无需也不得为算法增加自定义 Codec。

## 数据契约

默认使用下面的极简变长契约：

```cpp
struct PulseCompressionHeader {
    std::uint32_t pulses;
    std::uint32_t samples_per_pulse;
};

struct PulseCompressionFrame {
    PulseCompressionHeader header;
    std::vector<CS16> payload;
};

struct RDMapHeader {
    std::uint32_t rows;
    std::uint32_t cols;
};

struct RDMapFrame {
    RDMapHeader header;
    std::vector<float> payload;
};
```

SDK 按成员名自动识别 `header + payload`：`header` 必须平凡可复制，`payload`
必须是默认 allocator 的 `std::vector<Element>`，且 `Element` 平凡默认构造并
平凡可复制。Wire
只包含 Header 和 vector 的有效元素，不传输 vector 对象本身、capacity 或指针。
业务元数据统一放在 `header` 中，不要在帧结构体增加第三个待传输成员。

仍可使用完全平凡可复制的固定 POD（标量、平凡嵌套结构、`std::array`），SDK
会自动走固定帧路径。两种形式都不需要 `codec.h`。禁止 `std::string`、指针、
智能指针、Span/View、虚函数和其他外部内存所有权。

生产流图中，变长帧的 Block 参数必须给出包含 32B Envelope 的最大 Wire 长度：

```yaml
params:
  max_input_frame_bytes: 1048616
  max_output_frame_bytes: 1048616
```

SDK 用该上限一次性建立暂存区并 reserve vector。算法仍应校验 Header 中的业务
形状与 `payload.size()` 一致，业务不合法时返回 `Drop`。

## 算法接口

```cpp
class MyAlgorithm {
public:
    using InputData = PulseCompressionFrame;//此处定义传入的数据结构体
    using OutputData = RDMapFrame;//此处定义传出的数据结构体

    explicit MyAlgorithm(const cycore::sdk::Params& params);

    cycore::sdk::ProcessResult work(
        const InputData& input,
        OutputData& output) noexcept;
};
```

插件只通过：

```cpp
CYCORE_EXPORT_FRAME_ALGORITHM(
    "my_plugin",
    "algorithm.my_block",
    MyAlgorithm)
```

导出。插件名、`.so`、Block key 和部署配置必须一致。

## 算法编译与测试

从 `uestcradar` 仓库根目录执行以下命令，完成算法插件的本地编译与正确性单测：

```bash
# 命令说明：
# 1. algo_dir=.agents/...          : 设置待编译算法代码的目录路径
# 2. cmake -S "$algo_dir" -B ...   : 配置 CMake 工程（Release 优化模式并开启单元测试）
# 3. cmake --build ... -j$(nproc)  : 调用多核 CPU 并行编译算法插件 (.so) 与测试程序
# 4. ctest --test-dir ...          : 运行算法自动化单测，失败时打印详细日志

algo_dir=.agents/skills/develop_cpp_algorithm/algorithm_template

cmake -S "$algo_dir" -B /tmp/build-my-algorithm \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON

cmake --build /tmp/build-my-algorithm -j"$(nproc)"

ctest --test-dir /tmp/build-my-algorithm --output-on-failure
```

## 吞吐率性能测试

开发者只需在算法基准测试脚本中填充代表性的单帧输入数据：

```cpp
#include <cycore_benchmark_harness.h>
#include "algorithm.h"

CYCORE_REGISTER_BENCHMARK(
    MyAlgorithm,
    [](MyAlgorithm::InputData& input) {
        input.header.sample_count =
            cycore::algorithm::my_block::kMaxSamples;
        input.payload.resize(input.header.sample_count);
        // 开发者在此填充代表性雷达信号数据
    });
```

Harness 根据代表性输入输出的实际编码长度自动建立完整字节流链路，完成 Envelope、
调度、输出消费、sequence 校验和指标计算。算法 Benchmark 禁止手写 Port、连接、
封帧、`peek_copy`、`consume_exact`、计时和吞吐公式。

输出包含 frames/s、payload_gib_per_s、average_latency_us、
framework_allocations 和 checksum。稳态分配非零时返回状态码 `2`。

运行算法性能基准测试程序：

```bash
# 参数含义：
# /tmp/build-my-algorithm/bm_algorithm_block <最小运行毫秒> <预热次数> <最少执行次数>

/tmp/build-my-algorithm/bm_algorithm_block 3000 500 1000
```

> **注意**：最终交付的目标环境插件必须采用 ARM64 交叉编译生成 `.so` 文件。

## 交叉编译（ARM64 架构）

从 `uestcradar` 根目录构建交叉编译 Docker 镜像：

```bash
# 命令说明：
# 1. algo_dir=.agents/...       : 设置算法代码所在目录路径
# 2. docker build -t ... -f ... : 构建 ARM64 交叉编译 Docker 镜像

algo_dir=.agents/skills/develop_cpp_algorithm/algorithm_template

docker build -t uestcradar-template-build \
  -f "$algo_dir/Dockerfile.build_cross" .
```

挂载模板目录，在容器内交叉编译出 ARM64 架构的 `.so` 动态库：

```bash
# 命令参数说明：
# 1. docker run --rm                    : 启动 Docker 容器，运行结束后自动清理销毁容器
# 2. --user "$(id -u):$(id -g)"          : 传入宿主机当前用户 UID:GID，防止编译产物属主变为 root
# 3. -v "$(pwd)/$algo_dir:/workspace"    : 挂载自带 SDK 快照的算法模板
# 4. -w /workspace/.../algorithm_template : 指定容器启动后的初始工作目录
# 5. uestcradar-template-build           : 所使用的 ARM64 交叉编译 Docker 镜像名称
# 6. bash -lc "..."                      : 启动登录 Shell 执行 CMake 交叉编译与构建命令
# 7. -DCMAKE_BUILD_TYPE=Release          : 开启 Release 优化编译 (-O3)
# 8. -DCMAKE_CXX_COMPILER=aarch64-...    : 显式指定 C++ 编译器为 ARM64 交叉编译器 aarch64-linux-gnu-g++
# 9. cmake --build build-cross -j$(nproc): 使用多核 CPU 并行构建，生成 ARM64 动态库 my_plugin.so

docker run --rm \
  --user "$(id -u):$(id -g)" \
  -v "$(pwd)/$algo_dir:/workspace" \
  -w /workspace \
  uestcradar-template-build \
  bash -lc "cmake -S . -B build-cross \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_COMPILER=aarch64-linux-gnu-g++ \
    -DBUILD_TESTING=ON && \
    cmake --build build-cross -j\$(nproc)"
```

最终在 ARM64 实际硬件或仿真环境中运行 `qa_algorithm_block` 与 Benchmark。
原生 Header/POD Wire 不承诺 x86_64 与 AArch64 直接交换；两端必须分别确认 ABI 契约。

## 交付产物

最终交付：

- 算法源码及已适配的两个测试文件
- 全部正确性测试通过的记录
- ARM64 目标环境的 Release Benchmark 配置和结果
- `build-cross/my_plugin.so`

交付前再次确认插件名、`.so` 文件名和 Block key 与部署配置一致。不得用本地
x86_64 编译的 `.so` 代替交叉编译产物，也不得修改模板内 `sdk/include` 的 SDK 快照。
