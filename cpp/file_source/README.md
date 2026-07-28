# FileSource

原生 `cy::flowgraph::Block`，循环读取 CS16 `.bin`，输出带 32B Envelope 的变长
IQFrame。参数为 `file_path`、`pulses`、`samples_per_pulse`，可选
`initial_sequence_id`。

文件必须由完整的原生端序 CS16 和完整 pulse 组成。EOF 不跨界补帧：不足
`pulses` 的最后一段作为变长尾帧发送，随后从文件头继续，sequence 不重置。
连接容量至少为：

```text
32 + 8 + pulses * samples_per_pulse * 4
```

```yaml
type: file_source
plugin: file_source.so
params:
  file_path: /data/iq.bin
  pulses: 64
  samples_per_pulse: 4096
  initial_sequence_id: 0
```

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

目录包含独立 SDK 头文件快照，不引用目录外源码。
