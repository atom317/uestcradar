# Cycore C++ 算法开发模板

本模板用于开发 Cycore 算法插件。按“定义数据 → 实现算法 → 导出插件 → 编译 →
正确性测试 → 性能测试 → 交付”的顺序操作即可。

## 模板结构与修改范围

```text
algorithm_template/
├── CMakeLists.txt
├── Dockerfile.build_cross
├── include/
│   ├── data.h                  # 算法自定义的 InputData/OutputData
│   ├── codec.h                 # 业务 Payload 编解码
│   └── algorithm.h             # 算法类声明
├── src/
│   ├── algorithm.cpp           # 算法实现
│   └── algorithm_block.cpp     # 插件注册和导出
├── test/
│   ├── qa_algorithm_block.cpp  # 正确性测试
│   ├── qa_plugin_load.cpp      # 真实 .so 加载测试
│   └── bm_algorithm_block.cpp  # 性能测试
└── sdk/include/                # 只读 Cycore SDK 头文件
```

| 文件                            | 是否修改                                 |
| ------------------------------- | ---------------------------------------- |
| `include/data.h`              | 修改：算法自定义的输入输出强类型         |
| `include/codec.h`             | 修改：业务 Payload 编解码与严格校验      |
| `include/algorithm.h`         | 修改：算法类声明                         |
| `src/algorithm.cpp`           | 修改：算法实现                           |
| `src/algorithm_block.cpp`     | 修改：插件标识和导出类型                 |
| `test/qa_algorithm_block.cpp` | 修改：正确性测试数据                     |
| `test/qa_plugin_load.cpp`     | 修改：真实`.so` 加载测试数据           |
| `test/bm_algorithm_block.cpp` | 修改：性能测试配置                       |
| `CMakeLists.txt`              | 通常不改；新增依赖或修改插件目标名时才改 |
| `Dockerfile.build_cross`      | 通常不改                                 |
| `sdk/include/**`              | 禁止修改；只通过 Cycore SDK 同步更新     |

同时禁止修改算法构造函数、`work()` 生产接口、插件导出符号、接口版本和注册方式，也
不要为测试创建另一套算法入口。

## 数据契约

在 `include/data.h` 中定义算法自己的 `InputData` 和 `OutputData`。它们可以是算法
开发者定义的任意业务结构体，SDK 不规定字段名称、维度、元素类型、容器类型或内存
布局，也不要求结构体可平凡复制。当前 Adapter 只要求类型可以默认构造，并存在对应的
`FrameCodec<T>`。

例如，距离-多普勒算法可以直接把脉冲压缩结果作为 `InputData`，把 RD 图作为
`OutputData`：

```cpp
// 脉冲压缩结果：纯业务语义定义，不包含 SDK 传输帧头。
struct PulseCompressionFrame {
    std::uint32_t pulses;               // 脉冲数 M，例如 64
    std::uint32_t samples_per_pulse;    // 单脉冲采样点数 N，例如 4096
    std::vector<cy::common::CS16> payload;
    // payload.size() == pulses * samples_per_pulse
};

// RD 图：纯业务语义定义，不包含 SDK 传输帧头。
struct RDMapFrame {
    std::uint32_t rows;                 // Doppler Bins，即脉冲数 M
    std::uint32_t cols;                 // Range Bins，即采样点数 N
    std::vector<float> payload;
    // payload.size() == rows * cols
};

using InputData = PulseCompressionFrame;
using OutputData = RDMapFrame;
```

在 `include/codec.h` 中分别实现 `FrameCodec<InputData>` 和
`FrameCodec<OutputData>`。包含 `std::vector` 的对象不能通过 `memcpy` 直接传输；Codec
应把维度字段和容器元素编码为连续业务 Payload，解码时检查乘法溢出、总字节数以及
`payload.size()` 是否与维度严格一致。

## 算法开发规范

### 算法类

`include/algorithm.h` 保留以下接口：

```cpp
class MyAlgorithm {
public:
    using InputData = cycore::algorithm::my_block::InputData;
    using OutputData = cycore::algorithm::my_block::OutputData;

    explicit MyAlgorithm(const cycore::sdk::Params& params);

    cycore::sdk::ProcessResult work(const InputData& input,
                                    OutputData& output) noexcept;
};
```

构造函数在 `src/algorithm.cpp` 中读取并校验 `Params`。固定尺寸算法可以继续使用
固定尺寸，不要求改成动态参数。

### work() 实现

每次 `work()` 完成一帧处理：

1. 校验业务字段和维度；
2. 直接读取完整 `InputData`；
3. 将结果写入预分配的 `OutputData`；
4. 返回 `Produced`、`Retry` 或 `Drop`。

算法不得解析 SDK Envelope、操作底层字节流、手动消费输入、手动封包或透传元数据。

### 插件导出

`src/algorithm_block.cpp` 只负责生产插件导出：

```cpp
CYCORE_EXPORT_FRAME_ALGORITHM(
    "my_plugin",
    "algorithm.my_block",
    MyAlgorithm)
```

插件名、CMake 目标名、生成的 `.so` 文件名、Block key、部署配置和
`test/qa_plugin_load.cpp` 必须一致。

## 编译构建

### 本地开发编译

本地编译适合前期快速检查，不作为最终交付产物：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

默认只生成 `build/my_plugin.so`，不会构建测试和 Benchmark。

### ARM64 交叉编译

部署到 ARM64 目标机时，最终 `.so` 必须交叉编译。先构建一次交叉编译镜像：

```bash
docker build -t uestcradar-template-build \
  -f Dockerfile.build_cross .
```

再在模板根目录构建生产插件和最终测试程序：

```bash
docker run --rm \
  --user "$(id -u):$(id -g)" \
  -v "$(pwd):/workspace" \
  -w /workspace \
  uestcradar-template-build \
  bash -lc "cmake -S . -B build-cross \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_COMPILER=aarch64-linux-gnu-g++ \
    -DBUILD_TESTING=ON \
    -DBUILD_BENCHMARKS=ON &&
    cmake --build build-cross -j\$(nproc)"
```

交付产物是 `build-cross/my_plugin.so`，不是本地 `build/my_plugin.so`。交付前可执行
`file build-cross/my_plugin.so`，确认结果为 `ARM aarch64`。

## 正确性测试

在 `test/qa_algorithm_block.cpp` 中填写真实 `Params`、业务输入、期望输出和最大线帧
字节数。在 `test/qa_plugin_load.cpp` 中填写插件名、Block key 和一帧验证数据。

前期可在本机执行：

```bash
cmake -S . -B build-test \
  -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_TESTING=ON
cmake --build build-test -j"$(nproc)"
ctest --test-dir build-test --output-on-failure
```

测试必须通过 Codec、数值正确性、逐字节残帧、环形折返、输出背压、元数据透传和
真实 `.so` 加载检查。动态插件的物理输入输出端口必须是 `std::byte`。最终交付前，
还必须在 ARM64 目标环境运行交叉编译出的：

```bash
./build-cross/qa_algorithm_block
./build-cross/qa_plugin_load ./build-cross/my_plugin.so
```

算法核心单元测试可以保留，但不能替代上述测试。

## 性能测试

`test/bm_algorithm_block.cpp` 测量完整的
`std::byte → FrameAlgorithmAdapter → std::byte` 路径，包括拆帧、Codec、算法计算、
封包和输出重组。

### 第一步：写 Params 和业务帧

填写创建生产 Block 所需的 `ValueMap`，并构造一个符合 Codec 契约的代表性输入帧。

### 第二步：设置最大线帧字节数

`max_input_frame_bytes`、`max_output_frame_bytes` 和测试连接容量必须覆盖代表性输入输出
线帧。容量无需是帧长的整数倍。

### 第三步：消费输出结果

计时循环必须读取并使用输出结果，防止编译器删除算法计算。保留命令行的测量时长、
预热次数、最少调用次数和 case 过滤参数。

### 规则与约束说明

* **测量口径**：输出 frames/s、Payload GiB/s 和平均单帧延迟。
* **正确性**：Benchmark 启动前必须先通过全部 QA。
* **开发红线**：不得把纯算法循环结果冒充完整 Adapter 吞吐量。

前期可在本机运行 Release Benchmark：

```bash
cmake -S . -B build-benchmark \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_BENCHMARKS=ON
cmake --build build-benchmark -j"$(nproc)"
./build-benchmark/bm_algorithm_block
```

最终性能结果必须在 ARM64 目标环境使用交叉编译程序运行：

```bash
./build-cross/bm_algorithm_block
```

需要更长测试时可传入最短测量毫秒数、预热次数和最少正式调用次数；第四个可选参数按 case 名称过滤：

```bash
./build-cross/bm_algorithm_block 3000 500 1000 [case_filter]
```

结果包含调用数、帧吞吐、Payload 带宽和平均延迟，表示当前目标机上完整算法 Block 的饱和处理上限，不代表设备、网络、磁盘或整套系统性能。

## 交付产物

最终交付：

- 算法源码及已适配的三个测试文件
- 全部正确性测试通过的记录
- ARM64 目标环境的 Release Benchmark 配置和结果
- `build-cross/my_plugin.so`

交付前再次确认插件名、`.so` 文件名和 Block key 与部署配置一致。不得用本地
x86_64 编译的 `.so` 代替交叉编译产物，也不得交付对 `sdk/include/**` 的修改。
