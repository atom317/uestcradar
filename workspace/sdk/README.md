# UESTC Radar SDK

> **📢 提示：如果下面的数据字段定义的不合理，请联系 SDK 开发者进行修改。**

算法只需包含两个头文件：

```cpp
#include <data.h>
#include <sdk.h>
```

`data.h` 定义三类核心数据帧：

- `IQFrame`：`ComplexInt16[channel][time]`
- `PulseCompressionFrame`：`ComplexFloat32[channel][range_bin]`
- `RDFrame`：`float[range_bin][doppler_bin]`

---

## 算法开发者 API 手册

### 1. 核心端口与主要方法

| 端口类别           | 接口类型          | 作用                   | 核心方法与用法示例                                            |
| :----------------- | :---------------- | :--------------------- | :------------------------------------------------------------ |
| **输入端口** | `Input<Frame>`  | 接收并读取上游数据帧   | `auto frame = input.read();`                                |
| **输出端口** | `Output<Frame>` | 创建并向下游发送数据帧 | `auto frame = output.create(metadata);output.write(frame);` |

---

### 2. 数据帧的读写规则

| 帧来源                                 |          读写权限          | 使用说明                                                                                                                                     |
| :------------------------------------- | :------------------------: | :------------------------------------------------------------------------------------------------------------------------------------------- |
| **输入帧** (`input.read()`)    | **只读 (Read-Only)** | 上游发来的帧数据，直接读取其`frame.metadata` 与 `frame.data[row][col]` 属性。离开当前作用域后自动释放缓冲区槽位。                        |
| **输出帧** (`output.create()`) | **可写 (Writable)** | 由算法自己创建的帧，需填写`frame.metadata` 并往 `frame.data[row][col]` 填入计算结果。填完后调用 `output.write(frame)` 提交发送给下游。 |

---

### 3. 数据帧的常用属性

无论是输入帧还是输出帧，都具备以下两个常用属性：

| 帧属性                       | 类型                   | 作用与示例                                                                                                       |
| :--------------------------- | :--------------------- | :--------------------------------------------------------------------------------------------------------------- |
| **`frame.metadata`** | 结构体 (`Metadata`)  | 访问帧的一些基本参数，可以通过这个结构体传输一些雷达参数。                                                       |
| **`frame.data`**     | 二维矩阵 (`Array2D`) | 访问二维数据。• 读取/写入：`frame.data[row][col]`• 查询维度：`frame.data.rows()`、`frame.data.columns()` |

---

### 4. Metadata 中与二维数据尺寸（Dimensions）关联的属性

在调用 `output.create(metadata)` 创建输出帧时，系统会根据 `metadata` 中指定的尺寸属性自动分配二维数据矩阵大小：

| 数据帧类型                          | 矩阵行数`data.rows()` 对应的属性         | 矩阵列数`data.columns()` 对应的属性              |
| :---------------------------------- | :----------------------------------------- | :------------------------------------------------- |
| **`IQFrame`**               | **`channel_count`** (通道数)       | **`samples_per_channel`** (单通道采样点数) |
| **`PulseCompressionFrame`** | **`channel_count`** (通道数)       | **`range_bin_count`** (距离门数量)         |
| **`RDFrame`**               | **`range_bin_count`** (距离门数量) | **`doppler_bin_count`** (多普勒通道数)     |

> **⚠️ 尺寸规则**：`output.create(metadata)` 传入的尺寸属性必须大于 0，且不能超过侧边栏网关设置的最大单帧字节上限。

---

## 核心数据结构体详细定义

#### 1) `IQMetadata` (IQ 数据帧元数据)

```cpp
struct IQMetadata {
    std::uint64_t frame_id;           // 递增帧 ID
    std::uint64_t timestamp_unix_ns;  // 纳秒级 UNIX 时间戳
    std::uint32_t channel_count;      // 【尺寸相关】矩阵行数
    std::uint32_t samples_per_channel;// 【尺寸相关】矩阵列数
    double        sample_rate_hz;     // 采样率 (Hz)
    double        center_frequency_hz;// 中心频率 (Hz)
};
```

* **采样点类型**：`ComplexInt16` (`{ int16_t i; int16_t q; }`)

#### 2) `PulseCompressionMetadata` (脉冲压缩帧元数据)

```cpp
struct PulseCompressionMetadata {
    std::uint64_t frame_id;           // 递增帧 ID
    std::uint64_t timestamp_unix_ns;  // 纳秒级 UNIX 时间戳
    std::uint32_t channel_count;      // 【尺寸相关】矩阵行数
    std::uint32_t range_bin_count;    // 【尺寸相关】矩阵列数
    std::uint32_t pulse_index;        // 当前 CPI 内脉冲索引
    std::uint32_t pulses_per_cpi;     // CPI 积累脉冲总数
    double        range_resolution_m; // 距离分辨率 (米)
};
```

* **采样点类型**：`ComplexFloat32` (`{ float i; float q; }`)

#### 3) `RDMetadata` (距离多普勒帧元数据)

```cpp
struct RDMetadata {
    std::uint64_t frame_id;               // 递增帧 ID
    std::uint64_t timestamp_unix_ns;      // 纳秒级 UNIX 时间戳
    std::uint32_t channel_index;          // 当前通道索引
    std::uint32_t range_bin_count;        // 【尺寸相关】矩阵行数
    std::uint32_t doppler_bin_count;      // 【尺寸相关】矩阵列数
    double        range_resolution_m;     // 距离分辨率 (米)
    double        velocity_resolution_mps;// 速度分辨率 (米/秒)
};
```

* **采样点类型**：`float` (实数能量/幅值)

---

## 典型算法开发示范

```cpp
using namespace uestcradar;

Input<IQFrame> input;
Output<PulseCompressionFrame> output;

for (;;) {
    // 1. 读取输入帧 (只读)
    auto iq = input.read();

    // 2. 根据元数据创建输出帧 (可写，填入尺寸属性)
    auto pulse = output.create({
        .frame_id = iq.metadata.frame_id,
        .timestamp_unix_ns = iq.metadata.timestamp_unix_ns,
        .channel_count = iq.metadata.channel_count,            // 行数
        .range_bin_count = iq.metadata.samples_per_channel,     // 列数
        .pulse_index = 0,
        .pulses_per_cpi = 128,
        .range_resolution_m = 1.5,
    });

    // 3. 填写计算结果
    pulse.data[0][0] = {1.0F, 0.0F};

    // 4. 提交给下游
    output.write(pulse);
}
```

---


## SDK 发布流程

> **⚠️ 注意：本目录仅供SDK开发人员阅读**
>
> 本目录包含了雷达核心通信 SDK 的源码。算法开发人员无需自行编译 SDK，只需要基于本目录构建并发布的 `ring-algo-base` 镜像进行开发即可。

### 构建并发布 SDK 基础镜像

当您修改了 `sdk.cpp` 或 `sdk.h` 的底层共享内存/网络通信逻辑后，需要重新打包系统级的“算法基础镜像”并推送到私有仓库。

请按照以下步骤执行发布：

```bash
# 1. 必须退回项目根目录执行构建（以包含 common/ringbuf 依赖）
cd ../../../
docker build -t registry.chengyistudio.com/cxx/algo-base:latest -f workspace/sdk/Dockerfile .

# 2. 如果您需要将其标记为最新版本 (可选)
docker tag registry.chengyistudio.com/cxx/algo-base:latest registry.chengyistudio.com/cxx/algo-base:latest

# 3. 登录并推送至私有源
docker login registry.chengyistudio.com
docker push registry.chengyistudio.com/cxx/algo-base:latest
```

发布完成后，算法开发团队的 `Dockerfile` 第一行 `FROM` 语句拉取到的基础镜像（`algo-base`）就会自动包含您最新编译的 `/usr/local/lib/libuestcradar_sdk.so` 和 `/usr/local/include/uestcradar/sdk.h` 了。
