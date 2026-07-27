# 输入输出 POD 数据规范

Flowgraph 边的物理类型固定为 `std::byte`。SDK 负责 32B Envelope、拆帧、
长度校验、序列统计、元数据透传和输出封包；算法只定义固定大小的业务对象。

## 1. 类型约束

`InputData` 和 `OutputData` 必须默认可构造且满足：

```cpp
std::is_trivially_copyable_v<T>
```

允许基础标量、枚举、平凡嵌套结构和 `std::array`。禁止 `std::vector`、
`std::string`、裸指针、智能指针、Span/View、虚函数及拥有外部内存的成员。
C++17 无法从 `is_trivially_copyable` 自动识别原始指针的传输语义，因此指针禁令
同时属于代码审查硬约束。

```cpp
inline constexpr std::size_t kMaxSamples = 4096;

struct InputData {
    std::uint32_t sample_count = 0;
    std::array<cy::common::CS16, kMaxSamples> samples{};
};

static_assert(
    std::is_trivially_copyable_v<InputData>,
    "Non-trivial data requires a custom FrameCodec, POD only");
```

输入对象必须使用 `{}` 初始化后再填写有效字段；算法每次返回 `Produced` 前必须
确定性覆盖或清零 `OutputData` 的全部成员，避免未使用数组区域携带上一帧数据。

## 2. 固定 Wire 契约

SDK 使用 `TrivialFrameCodec<T>` 自动执行整块 `memcpy`：

```text
[32B SDK Envelope][sizeof(T) 原生对象内存]
```

开发者不编写、特化或包含任何 Codec。`sample_count`、`rows`、`cols` 等字段只描述
固定容量对象中的有效区域，不改变 Payload 字节数。业务字段非法时由算法
`work()` 返回 `Drop`。

该协议采用原生对象表示，只支持兼容的编译器 ABI、结构体布局和字节序。调整字段
顺序、字段类型、数组容量或编译 ABI 都属于数据契约变更。

每条边必须满足：

```text
capacity >= 32 + sizeof(该边的数据类型)
```

容量不需要是帧长整数倍；残帧等待和环形折返由 SDK 处理。
