# uestcradar Algorithm SDK

`cpp/sdk/include` 是 SDK 维护真源。算法模板携带由此生成的稳定快照
`algorithm_template/sdk/include`，以支持脱离仓库的独立编译；业务算法不得自行
复制、生成或修改 SDK。

新帧级算法使用固定大小 POD 契约：

```text
Wire = 32B SDK Envelope + sizeof(InputData/OutputData)
```

`InputData`、`OutputData` 必须默认可构造并满足
`std::is_trivially_copyable_v<T>`。SDK 的 `TrivialFrameCodec<T>` 使用单次
`memcpy` 完成对象与 Payload 的转换；算法开发者不得新增业务 Codec。

该 Wire 是同 ABI 的原生对象表示。上下游必须使用兼容编译器 ABI、布局和字节序。
SDK 只提供 `std::byte` 帧边、`TrivialFrameCodec<T>`、`FrameAlgorithmAdapter`
和一键式 Benchmark Harness；不再提供点级 `Reader/Writer`、切片 View 或旧测试 Harness。

Release 吞吐矩阵覆盖 64B、1KiB、4KiB、64KiB、1MiB 和 8MiB：

```bash
cmake -S cpp -B /tmp/uestcradar-sdk-benchmark \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_BENCHMARKS=ON
cmake --build /tmp/uestcradar-sdk-benchmark \
  --target bm_pod_frame_sizes -j"$(nproc)"
/tmp/uestcradar-sdk-benchmark/bin/bm_pod_frame_sizes
```
