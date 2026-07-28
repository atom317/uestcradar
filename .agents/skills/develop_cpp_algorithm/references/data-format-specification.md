# 输入输出帧数据规范

Flowgraph 边的物理类型固定为 `std::byte`。SDK 负责 32B Envelope、增量拆帧、
长度校验、序列统计、元数据透传和输出封包；算法只处理完整强类型业务对象。

## 1. 默认变长帧契约

IQ 点数或矩阵尺寸运行时变化时，定义一个平凡 Header 和一个连续 vector：

```cpp
struct InputHeader {
    std::uint32_t pulses = 0;
    std::uint32_t samples_per_pulse = 0;
};

struct InputData {
    InputHeader header{};
    std::vector<cy::common::CS16> payload;
};

static_assert(std::is_trivially_copyable_v<InputHeader>);
static_assert(std::is_trivially_copyable_v<cy::common::CS16>);
```

成员名必须是 `header` 和 `payload`。`payload` 必须是默认 allocator 的
`std::vector<Element>`，Header 必须默认可构造且平凡可复制，Element 必须平凡
默认构造且平凡可复制。业务字段
全部放入 Header；帧结构体不要增加第三个待传输数据成员。禁止 `std::string`、
指针、智能指针、Span/View、虚函数和其他外部内存所有权。

SDK 自动编码为：

```text
[32B SDK Envelope][sizeof(Header) 原生 Header][N 个连续 Element]
```

不传输 `std::vector` 对象、指针、size 或 capacity。元素数由 Envelope 中的总字节
长度推导；Header 中的 rows/cols/count 属于业务校验，SDK 不解释，算法应验证它们
与 `payload.size()` 一致，不一致时返回 `Drop`。

变长帧必须配置：

```yaml
params:
  max_input_frame_bytes: 1048616
  max_output_frame_bytes: 1048616
```

数值包含 32B Envelope。Adapter 构造时按最大值一次性分配字节暂存区并 reserve
vector；完整帧超过上限、元素字节数不能整除或容量不足时不会触发 `work()`。

## 2. 固定 POD 兼容

尺寸真正固定时仍可直接定义默认可构造、平凡可复制的 POD：

```cpp
struct FixedInputData {
    std::uint32_t sample_count = 0;
    std::array<cy::common::CS16, 4096> payload{};
};

static_assert(std::is_trivially_copyable_v<FixedInputData>);
```

固定 Wire 为 `[32B SDK Envelope][sizeof(T) 原生对象]`，无需配置最大帧长。SDK
自动在固定 POD 与变长 vector 路径间选择，开发者不编写、特化或包含任何 Codec。

两种协议都采用原生 Header/POD 表示，只支持兼容的编译器 ABI、结构体布局和
字节序。调整字段顺序、字段类型或编译 ABI 都属于数据契约变更。连接容量至少应
容纳该边配置的最大完整 Wire 帧；容量不需要是帧长整数倍，残帧等待和环形折返由
SDK 处理。
