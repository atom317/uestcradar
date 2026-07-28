# SimSink

原生 `cy::flowgraph::Block`，增量接收 32B Envelope + IQFrame，校验 sequence、
timestamp、形状和循环 `.bin` 内容。数据错误累计统计后继续处理。

参考文件采用与 FileSource 相同的 CS16 原生端序和 EOF 变长尾帧规则。残帧不会
推进输入游标，完整坏帧被消费并记录到 `SimSinkStats`，不会阻塞后续合法帧。

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

参数为 `file_path`、`pulses`、`samples_per_pulse`，可选
`initial_sequence_id`。目录不引用外部源码。

```yaml
type: sim_sink
plugin: sim_sink.so
params:
  file_path: /data/iq.bin
  pulses: 64
  samples_per_pulse: 4096
  initial_sequence_id: 0
```
