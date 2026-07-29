# Telemetry

本模块是旁路只读观察者。

- 只认识由组合根注入的快照回调，不包含 RingBuffer 或 POSIX shared
  memory 依赖。
- 监控目标由 `TelemetryTarget` 动态描述；模块不硬编码共享内存名称和链路拓扑。
- 每个采样周期最多发送一个非阻塞 UDP Protobuf 数据报。
- DNS、网络或序列化失败只会结束 exporter 进程；入口脚本会重启它，且不影响两个主数据进程。

唯一入口是 `run_telemetry_exporter`。模块只调用回调并导出成功取得的快照，
不拥有被观察资源，也不向主数据链路回传控制信息。
