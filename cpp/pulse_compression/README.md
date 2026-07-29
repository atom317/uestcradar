# Pulse Compression Block

该目录是可独立构建的 Cycore 帧级脉冲压缩插件。算法只处理强类型
`InputData`/`OutputData`，SDK 负责字节流拆帧、Envelope、元数据透传和背压。

## 数据契约

输入和输出均采用：

```cpp
struct PulseCompressionHeader {
    std::uint32_t points;
};

struct InputData {
    PulseCompressionHeader header;
    std::vector<cy::common::CS16> payload;
};
```

默认一帧1024个CS16点，Wire长度为：

```text
32B SDK Envelope + 4B Header + 1024 × 4B CS16 = 4132B
```

`max_input_frame_bytes`、`max_output_frame_bytes` 和流图边容量均不得小于
4132字节。

## 波形与算法

- 采样率：30.72 MHz
- LFM带宽：20 MHz
- 参考信号：256点
- 脉宽：`256 / 30.72e6` 秒
- 起始频率：-10 MHz

算法在构造阶段创建FFTW Plan和匹配滤波频谱。`work()` 对输入做FFT、频域
点乘和IFFT，并从线性卷积的第255点开始裁剪，使输出峰值仍位于原目标距离门。
输出沿用旧CS16数值契约，除IFFT的 `1/NFFT` 外再按参考信号长度归一化。

空帧、点数不匹配或Payload长度不匹配时返回
`cycore::sdk::ProcessResult::Drop`。

## 独立构建

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON \
  -DBUILD_BENCHMARKS=ON
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

生成：

```text
build/pulse_compression.so
build/qa_pulse_compression_block
build/bm_pulse_compression_block
```

运行短基准：

```bash
./build/bm_pulse_compression_block 200 10 50
```

参数依次为最短运行毫秒数、预热帧数和最少测量帧数。Benchmark输出
`frames/s`、Payload GiB/s、平均延迟和框架稳态分配次数。
