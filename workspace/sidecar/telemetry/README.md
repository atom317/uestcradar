# Telemetry

本模块是旁路只读观察者。

- 使用 `O_RDONLY` 和只读映射采样 RingBuffer 头部。
- 每个采样周期最多发送一个非阻塞 UDP Protobuf 数据报。
- DNS、网络或序列化失败只会结束 exporter 进程；入口脚本会重启它，且不影响两个主数据进程。

唯一入口是 `run_telemetry_exporter`。模块不写共享内存，也不向主数据链路回传控制信息。
