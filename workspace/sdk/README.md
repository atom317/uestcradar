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
