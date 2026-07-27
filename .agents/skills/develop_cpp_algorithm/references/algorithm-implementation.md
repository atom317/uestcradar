# 算法核心实现规范 (Algorithm Implementation Guide)

在实现算子的核心 `work` 逻辑时，需遵循以下简明规范与执行顺序。

---

## 1. 配置参数

由于配置字典 `Params` 底层仅支持 `std::int64_t` 变体，为简化代码并避免类型转换，算子内部的整型属性（如通道数、采样点数）统一使用 `std::int64_t` 进行声明与提取。

```cpp
class MyAlgorithm {
public:
    explicit MyAlgorithm(const cycore::sdk::Params& params) {
        num_channels_ = params.get<std::int64_t>("num_channels", 16);
        fft_size_ = params.get<std::int64_t>("fft_size", 1024);
        if (num_channels_ <= 0 || fft_size_ <= 0) {
            throw std::invalid_argument("Dimensions must be positive");
        }
    }
private:
    std::int64_t num_channels_;
    std::int64_t fft_size_;
};
```

---

## 2. 处理强类型完整帧

算法声明自己的输入输出类型，并通过 `work()` 直接处理 SDK 已完成校验和解码的完整帧：

```cpp
class MyAlgorithm {
public:
    using InputData = my_algorithm::InputData;
    using OutputData = my_algorithm::OutputData;

    cycore::sdk::ProcessResult work(const InputData& input,
                                    OutputData& output) noexcept {
        if (!BusinessShapeIsValid(input)) {
            return cycore::sdk::ProcessResult::Drop;
        }
        // 直接计算并写入预分配的 output。
        return cycore::sdk::ProcessResult::Produced;
    }
};
```

- `Produced`：算法成功生成一个输出帧；SDK 自动编码、封包并透传元数据。
- `Retry`：保留当前已解码输入，等待条件满足后再次调用算法。
- `Drop`：丢弃当前业务帧且不产生输出。

算法内部不得解析 SDK Envelope、手动消费输入、处理环形折返、封装输出帧或复制 sequence/timestamp。

---

## 3. 数据寻址

通道、脉冲、行列和步长均属于算法自己的业务契约。应在 `InputData`、`OutputData` 或其辅助函数中集中定义索引公式，避免在传输层重复解释布局。

```cpp
constexpr std::size_t Index(std::size_t channel,
                            std::size_t pulse,
                            std::size_t sample,
                            std::size_t pulses,
                            std::size_t samples_per_pulse) noexcept {
    return (channel * pulses + pulse) * samples_per_pulse + sample;
}
```

固定上限的 `std::array`、预分配工作区和外部算法库计划应在构造阶段准备，避免稳态 `work()` 堆分配。

---

## 4. 闭环自检规范

测试必须使用生产 `FrameAlgorithmAdapter` 或动态加载后的真实 Block，输入端发布 SDK 线帧，输出端重新组装并解码完整线帧。至少验证：

1. Codec 正常往返及截断 Payload 拒绝；
2. 帧在每个字节位置切分时，残帧不触发算法且不推进读游标；
3. 环形缓冲区折返后仍能得到同一完整帧；
4. 输出背压不会导致算法重复执行；
5. sequence 和 timestamp 正确透传；
6. 数值结果满足理论解、金标数据和容差要求；
7. 动态插件端口的物理类型为 `std::byte`。

前端波形观察只能用于系统联调，不能替代自动化数值断言。

---

## 5. YAML 完整配置规范

算子参数仍在 `blocks` 中配置，同时为 Adapter 指定允许的最大输入输出线帧字节数：

```yaml
blocks:
  - id: my_algorithm
    type: algorithm.my_block
    plugin: my_plugin.so
    params:
      max_input_frame_bytes: 4194304
      max_output_frame_bytes: 4194304
```

连接容量必须满足：

```text
capacity >= 该连接允许的最大完整线帧字节数
```

容量不需要是帧长的整数倍。物理折返、分段读取和完整帧重组由 SDK Adapter 负责；算法不得依赖某一次物理读写窗口恰好连续。
