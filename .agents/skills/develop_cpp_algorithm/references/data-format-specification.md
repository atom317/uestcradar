# 输入输出数据格式规范

Flowgraph 边的物理类型固定为 `std::byte`，SDK 负责公共 Envelope、拆帧、完整性校验、序列统计和输出封包。算法只定义业务 Payload。

---

## 1. 业务强类型规范

算法开发者自行定义 `InputData` 和 `OutputData`。SDK 不限制业务字段，但类型必须可默认构造，且应使用固定容量存储避免稳态堆分配：

```cpp
struct InputData {
    std::uint32_t sample_count = 0;
    std::array<float, kMaxSamples> samples{};
};

struct OutputData {
    std::uint32_t sample_count = 0;
    std::array<float, kMaxSamples> samples{};
};
```

业务结构内部可以复用 `cy::common::CS16`、`CF32` 等项目类型，也可以定义算法需要的其他字段。不要把包含拥有型动态内存的对象直接按内存布局发送。

---

## 2. FrameCodec 规范

分别为输入和输出类型实现 `cycore::sdk::FrameCodec<T>`：

- `encoded_size()` 返回业务 Payload 的准确字节数；
- `encode()` 只写业务 Payload；
- `decode()` 严格检查长度、数量、维度和溢出后再构造强类型对象；
- Codec 不解析或生成 SDK Envelope，不处理 sequence 和 timestamp；
- Payload 长度可以逐帧变化，但不得超过配置的最大线帧字节数。

```cpp
template <>
struct FrameCodec<InputData> {
    static std::size_t encoded_size(const InputData& value);
    static bool encode(const InputData& value,
                       cy::common::Span<std::byte> payload) noexcept;
    static bool decode(cy::common::Span<const std::byte> payload,
                       InputData& value) noexcept;
};
```

边的 YAML `capacity` 必须不小于该连接允许的最大完整线帧字节数。无需把容量设置成帧长的整数倍；环形折返由 SDK 处理。
