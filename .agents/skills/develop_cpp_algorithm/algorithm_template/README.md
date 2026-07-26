# Cycore C++ 算法开发模板

本模板用于开发 Cycore 算法插件。按“定义数据 → 实现算法 → 导出插件 → 编译 →
正确性测试 → 性能测试 → 交付”的顺序操作即可。

## 模板结构与修改范围

```text
algorithm_template/
├── CMakeLists.txt
├── Dockerfile.build_cross
├── include/
│   ├── data.h                  # 输入输出类型和每次处理的元素数
│   └── algorithm.h             # 算法类声明
├── src/
│   ├── algorithm.cpp           # 算法实现
│   └── algorithm_block.cpp     # 插件注册和导出
├── test/
│   ├── block_test_harness.h    # 通用测试工具
│   ├── qa_algorithm_block.cpp  # 正确性测试
│   ├── qa_plugin_load.cpp      # 真实 .so 加载测试
│   └── bm_algorithm_block.cpp  # 性能测试
└── sdk/include/                # 只读 SDK 头文件
```

| 文件 | 是否修改 |
| --- | --- |
| `include/data.h` | 修改：输入输出类型和每次处理的元素数 |
| `include/algorithm.h` | 修改：算法类声明 |
| `src/algorithm.cpp` | 修改：算法实现 |
| `src/algorithm_block.cpp` | 修改：插件标识和导出类型 |
| `test/qa_algorithm_block.cpp` | 修改：正确性测试数据 |
| `test/qa_plugin_load.cpp` | 修改：真实 `.so` 加载测试数据 |
| `test/bm_algorithm_block.cpp` | 修改：性能测试配置 |
| `CMakeLists.txt` | 通常不改；新增依赖或修改插件目标名时才改 |
| `test/block_test_harness.h`、`Dockerfile.build_cross` | 通常不改 |
| `sdk/include/**` | 禁止修改 |

同时禁止修改算法构造函数、`work()` 生产接口、插件导出符号、接口版本和注册方式，也
不要为测试创建另一套算法入口。

## 数据契约

在 `include/data.h` 中定义 `InputSample`、`OutputSample` 和每次 `work()` 的输入输出
元素数。模板默认均为 `1 × 1024`：

```cpp
using InputSample = float;
using OutputSample = float;

constexpr std::size_t kInputElementsPerWork = 1024;
constexpr std::size_t kOutputElementsPerWork = 1024;
```

输入和输出可以类型不同、数量不同。样本类型必须可直接复制，可使用 `CS16`、`CF32`、
`float`、`int16_t`、`int32_t`、`uint8_t` 或 `std::byte`。

rows、cols、channels、pulses 等维度定义为算法常量或通过 `Params` 传入，不需要把
Matrix、Cube 定义成外部测试类型。

## 算法开发规范

### 算法类

`include/algorithm.h` 保留以下接口：

```cpp
class MyAlgorithm {
public:
    explicit MyAlgorithm(const cycore::sdk::Params& params);

    bool work(
        cycore::sdk::Reader<cycore::algorithm::my_block::InputSample>& in,
        cycore::sdk::Writer<cycore::algorithm::my_block::OutputSample>& out);
};
```

构造函数在 `src/algorithm.cpp` 中读取并校验 `Params`。固定尺寸算法可以继续使用
固定尺寸，不要求改成动态参数。

### work() 实现

每次 `work()` 完成一帧处理：

1. 使用 Reader 读取输入。
2. 使用 Writer 申请输出。
3. 完成计算并写满输出。
4. 成功返回 `true`；输入不足、输出空间不足或本帧失败时返回 `false`。

Reader 可使用 `read()`、`read_available()`、`read_matrix()`、`read_cube()`；
Writer 可使用 `reserve()`、`reserve_available()`、`reserve_matrix()`、
`reserve_cube()`。`std::byte` 端口可使用 SDK RawBytes 接口。不要手动提交输出或
消费输入。

### 插件导出

`src/algorithm_block.cpp` 只负责生产插件导出：

```cpp
CYCORE_EXPORT_ALGORITHM(
    "my_plugin",
    "algorithm.my_block",
    MyAlgorithm,
    cycore::algorithm::my_block::InputSample,
    cycore::algorithm::my_block::OutputSample
)
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

在 `test/qa_algorithm_block.cpp` 中填写真实 `Params`、每次输入/输出元素数、输入
数据、期望输出和缓冲容量。在 `test/qa_plugin_load.cpp` 中填写插件名、Block key
和一帧验证数据。

前期可在本机执行：

```bash
cmake -S . -B build-test \
  -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_TESTING=ON
cmake --build build-test -j"$(nproc)"
ctest --test-dir build-test --output-on-failure
```

测试必须通过数值正确性、输入不足、输出阻塞和真实 `.so` 加载检查。最终交付前，还
必须在 ARM64 目标环境运行交叉编译出的：

```bash
./build-cross/qa_algorithm_block
./build-cross/qa_plugin_load ./build-cross/my_plugin.so
```

算法核心单元测试可以保留，但不能替代上述测试。

## 性能测试

在 `test/bm_algorithm_block.cpp` 中填写真实 `Params`、输入/输出类型、每次输入元素数
`N`、输出元素数 `M`、缓冲容量和一帧预生成输入。可选填写目标输入速率
`Mitems/s`。支持多个运行尺寸时，在该文件中增加多个 case。

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

需要更长测试时可传入最短测量毫秒数、预热次数和最少正式调用次数：

```bash
./build-cross/bm_algorithm_block 3000 500 1000
```

结果包含成功/失败调用数、延迟、输入/输出吞吐和带宽，表示当前目标机上完整算法
Block 的饱和处理上限，不代表设备、网络、磁盘或整套系统性能。

## 交付产物

最终交付：

- 算法源码及已适配的三个测试文件
- 全部正确性测试通过的记录
- ARM64 目标环境的 Release Benchmark 配置和结果
- `build-cross/my_plugin.so`

交付前再次确认插件名、`.so` 文件名和 Block key 与部署配置一致。不得用本地
x86_64 编译的 `.so` 代替交叉编译产物，也不得交付对 `sdk/include/**` 的修改。
