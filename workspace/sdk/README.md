# UESTC Radar SDK

算法只需要两个头文件：

```cpp
#include <data.h>
#include <sdk.h>
```

`data.h` 定义三类数据：

- `IQFrame`：`ComplexInt16[channel][time]`
- `PulseCompressionFrame`：`ComplexFloat32[channel][range_bin]`
- `RDFrame`：`float[range_bin][doppler_bin]`

典型算法：

```cpp
using namespace uestcradar;

Input<IQFrame> input;
Output<PulseCompressionFrame> output;

for (;;) {
    auto iq = input.read();
    auto pulse = output.create({
        .frame_id = iq.metadata.frame_id,
        .timestamp_unix_ns = iq.metadata.timestamp_unix_ns,
        .channel_count = iq.metadata.channel_count,
        .range_bin_count = iq.metadata.samples_per_channel,
        .pulse_index = 0,
        .pulses_per_cpi = 128,
        .range_resolution_m = 1.5,
    });

    pulse.data[0][0] = {1.0F, 0.0F};
    output.write(pulse);
}
```

`read()` 和 `create()` 在 Ring 为空或满时阻塞。读 Frame 离开作用域自动释放；
未提交的写 Frame 离开作用域自动取消。大块二维数据直接映射共享内存 Slot，
SDK 只复制固定大小的 metadata。

SDK 只支持 C++20，不提供旧 C 接口。

构建与安装：

```bash
cmake -S workspace/sdk -B build/sdk \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build/sdk --parallel
ctest --test-dir build/sdk --output-on-failure
cmake --install build/sdk --prefix build/sdk-install
```

安装树的公共头文件只有 `include/sdk.h` 和 `include/data.h`。

---

## SDK 发布流程

> **⚠️ 注意：本目录仅供基建维护人员阅读**
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

