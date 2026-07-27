# uestcradar Algorithm SDK

`cpp/sdk/include` 是本仓库唯一算法 SDK 真源。算法模板和各 C++ 插件直接包含此
目录，不复制、生成或同步第二套 SDK。

新帧级算法使用固定大小 POD 契约：

```text
Wire = 32B SDK Envelope + sizeof(InputData/OutputData)
```

`InputData`、`OutputData` 必须默认可构造并满足
`std::is_trivially_copyable_v<T>`。SDK 的 `TrivialFrameCodec<T>` 使用单次
`memcpy` 完成对象与 Payload 的转换；算法开发者不得新增业务 Codec。

该 Wire 是同 ABI 的原生对象表示。上下游必须使用兼容编译器 ABI、布局和字节序。
现有旧 `Reader/Writer` 算子及其测试辅助设施暂时保留，待各算法独立迁移后清理。

Release 吞吐矩阵覆盖 64B、1KiB、4KiB、64KiB、1MiB 和 8MiB：

```bash
cmake -S cpp -B /tmp/uestcradar-sdk-benchmark \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_BENCHMARKS=ON
cmake --build /tmp/uestcradar-sdk-benchmark \
  --target bm_pod_frame_sizes -j"$(nproc)"
/tmp/uestcradar-sdk-benchmark/bin/bm_pod_frame_sizes
```
