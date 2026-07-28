# uestcradar Algorithm SDK

`cpp/sdk/include` 是 SDK 维护真源。算法模板携带由此生成的稳定快照
`algorithm_template/sdk/include`，以支持脱离仓库的独立编译；业务算法不得自行
复制、生成或修改 SDK。

SDK 自动支持两种 0 手写 Codec 契约：

```text
固定帧：Wire = 32B SDK Envelope + sizeof(T)
变长帧：Wire = 32B SDK Envelope + sizeof(T::header)
                                  + T::payload.size() * sizeof(Element)
```

固定帧类型必须默认可构造且满足 `std::is_trivially_copyable_v<T>`。变长帧
必须只用名为 `header` 的平凡可复制头和名为 `payload` 的
`std::vector<Element>` 表达传输数据，`Element` 必须平凡默认构造且平凡可复制。SDK 自动选择
`TrivialFrameCodec<T>` 或 `VectorFrameCodec<T>`；算法开发者不得新增业务 Codec。
变长帧必须在 YAML 中配置 `max_input_frame_bytes` 和
`max_output_frame_bytes`，Adapter 构造时据此一次性预留空间。

该 Wire 是同 ABI 的原生对象表示。上下游必须使用兼容编译器 ABI、布局和字节序。
SDK 只提供 `std::byte` 帧边、自动帧 Codec、`FrameAlgorithmAdapter`
和一键式 Benchmark Harness；不再提供点级 `Reader/Writer`、切片 View 或旧测试 Harness。

Release 吞吐矩阵同时覆盖固定 POD 与变长 vector 帧：

```bash
cmake -S cpp/sdk -B /tmp/uestcradar-sdk-benchmark \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_BENCHMARKS=ON
cmake --build /tmp/uestcradar-sdk-benchmark \
  --target bm_frame_sizes -j"$(nproc)"
/tmp/uestcradar-sdk-benchmark/bin/bm_frame_sizes
```
