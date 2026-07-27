# Cycore POD 帧级 C++ 算法模板

本模板用于在 `uestcradar` 仓库内开发帧级算法插件。开发者定义固定容量 POD
数据、实现数学逻辑并填写 QA/Benchmark 输入；SDK 自动完成拆帧、整块复制、
元数据透传和输出封包。

## 目录与修改范围

```text
algorithm_template/
├── CMakeLists.txt
├── Dockerfile.build_cross
├── include/
│   ├── data.h                  # POD InputData/OutputData
│   └── algorithm.h             # 算法接口
├── src/
│   ├── algorithm.cpp           # 数学实现
│   └── algorithm_block.cpp     # 插件注册
└── test/
    ├── qa_algorithm_block.cpp  # 算法功能与数学正确性
    └── bm_algorithm_block.cpp  # 一键式性能基准
```

模板没有 SDK 副本，所有目标只使用仓库唯一真源 `cpp/sdk/include`。模板脱离
`uestcradar` 目录单独复制后不会配置成功。

开发者通常只修改 `data.h`、算法头/实现、插件标识和两个测试的业务输入/断言。
不存在 `codec.h`，不得为算法增加自定义 Codec。

## POD 数据契约

`InputData` 和 `OutputData` 必须默认可构造且满足：

```cpp
std::is_trivially_copyable_v<T>
```

只允许标量、枚举、平凡嵌套结构和 `std::array`。禁止 `std::vector`、
`std::string`、指针、智能指针、Span/View、虚函数和外部内存所有权。

```cpp
inline constexpr std::size_t kMaxPulses = 64;
inline constexpr std::size_t kMaxSamplesPerPulse = 4096;
inline constexpr std::size_t kMaxValues =
    kMaxPulses * kMaxSamplesPerPulse;

struct PulseCompressionFrame {
    std::uint32_t pulses = 0;
    std::uint32_t samples_per_pulse = 0;
    std::array<cy::common::CS16, kMaxValues> payload{};
};

struct RDMapFrame {
    std::uint32_t rows = 0;
    std::uint32_t cols = 0;
    std::array<float, kMaxValues> payload{};
};
```

每帧 Payload 固定为 `sizeof(T)`。维度字段只说明数组中的有效区域，不改变传输
长度。输入使用 `{}` 初始化后再填写；每次返回 `Produced` 前必须确定性覆盖或清零
`OutputData` 的全部成员，避免未使用数组区域携带上一帧数据。业务维度由
`work()` 校验，非法业务字段返回 `Drop`。

Wire 使用原生对象布局：

```text
[32B SDK Envelope][sizeof(T) POD Payload]
```

因此上下游必须使用兼容的编译器 ABI、结构体布局和字节序。修改字段、顺序、类型或
数组容量均属于数据契约变更。

## 算法接口

```cpp
class MyAlgorithm {
public:
    using InputData = cycore::algorithm::my_block::InputData;
    using OutputData = cycore::algorithm::my_block::OutputData;

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

## 本地构建

从 `uestcradar` 仓库根目录执行：

```bash
template=.agents/skills/develop_cpp_algorithm/algorithm_template
cmake -S "$template" -B /tmp/uestcradar-algorithm-template \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON
cmake --build /tmp/uestcradar-algorithm-template -j"$(nproc)"
ctest --test-dir /tmp/uestcradar-algorithm-template --output-on-failure
```

## 吞吐率测试

开发者只填充一帧输入：

```cpp
#include <cycore_benchmark_harness.h>
#include "algorithm.h"

CYCORE_REGISTER_BENCHMARK(
    MyAlgorithm,
    [](MyAlgorithm::InputData& input) {
        input.sample_count =
            cycore::algorithm::my_block::kMaxSamples;
        // 填充代表性数据。
    });
```

Harness 根据 `sizeof(InputData/OutputData)` 自动建立完整字节流链路，完成 Envelope、
调度、输出消费、sequence 校验和指标计算。算法 Benchmark 禁止手写 Port、连接、
封帧、`peek_copy`、`consume_exact`、计时和吞吐公式。

输出包含 frames/s、payload_gib_per_s、average_latency_us、
framework_allocations 和 checksum。稳态分配非零时返回状态码 `2`。

```bash
/tmp/uestcradar-algorithm-template/bm_algorithm_block 3000 500 1000
```

必须采用ARM64 交叉编译得到so文件

## 交叉编译

从 `uestcradar` 根目录构建镜像：

```bash
template=.agents/skills/develop_cpp_algorithm/algorithm_template
docker build -t uestcradar-template-build \
  -f "$template/Dockerfile.build_cross" .
```

挂载整个仓库，使模板和唯一 SDK 同时可见：

```bash
docker run --rm \
  --user "$(id -u):$(id -g)" \
  -v "$(pwd):/workspace" \
  -w /workspace/.agents/skills/develop_cpp_algorithm/algorithm_template \
  uestcradar-template-build \
  bash -lc "cmake -S . -B build-cross \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_COMPILER=aarch64-linux-gnu-g++ \
    -DBUILD_TESTING=ON &&
    cmake --build build-cross -j\$(nproc)"
```

最终在 ARM64 环境运行 `qa_algorithm_block` 和 Benchmark。
原生 POD Wire 不承诺 x86_64 与 AArch64 直接交换；两端必须分别确认 ABI 契约。

## 交付产物

最终交付：

- 算法源码及已适配的两个测试文件
- 全部正确性测试通过的记录
- ARM64 目标环境的 Release Benchmark 配置和结果
- `build-cross/my_plugin.so`

交付前再次确认插件名、`.so` 文件名和 Block key 与部署配置一致。不得用本地
x86_64 编译的 `.so` 代替交叉编译产物，也不得随单个算法复制或修改 SDK；SDK
变更只能发生在仓库唯一真源 `cpp/sdk/include`。
