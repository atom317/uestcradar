# RingBuffer Telemetry 设计

## 目标

- 每100ms展示容量、已用空间、读写位置和使用率曲线。
- Telemetry 单向、限速、允许丢包，不向主数据链路施加反压。
- 支持多个物理节点汇聚到一个中央页面。

## 组件

```text
每个节点
└── Sidecar 容器
    ├── produce-upstream       关键进程
    ├── consume-downstream     关键进程
    └── export-telemetry       非关键进程
              │ UDP + Proto
              ▼
集群中央
└── Telemetry Web 容器
    ├── UDP 接收
    ├── 有界内存历史
    ├── HTTP API
    └── 前端
```

每个节点不再部署独立 Agent。Exporter 属于 Sidecar，但以独立进程运行；它退出后
单独重启，不触发 Sidecar 容器退出。两个数据进程任一退出时，容器停止并清理其余进程。

## RingBuffer ABI

RingBuffer 前4096字节是固定头部：

- `magic`、`abi_version`、`header_size`
- `capacity_bytes`
- `write_position`、`read_position`
- `shutdown`

数据区从下一页开始。Exporter 使用 `O_RDONLY + PROT_READ`，只映射头部，不映射数据区。
C++ 通过 `static_assert` 固定字段偏移。

快照最多重试三次，只接受：

```text
write_position >= read_position
write_position - read_position <= capacity_bytes
```

不一致的快照直接丢弃。

## Telemetry 数据流

```text
RingBuffer header
  -> Sidecar Exporter 10Hz snapshot
  -> generated C++ Protobuf
  -> non-blocking UDP
  -> generated Go Protobuf
  -> bounded in-memory history
  -> HTTP JSON
  -> Browser 100ms refresh
```

Exporter 使用约1400字节固定发送缓冲区和 `MSG_DONTWAIT`，无发送队列、无确认、无重传。
解析、序列化或发送失败只丢弃当前快照。Web 不向 Sidecar发送控制消息。

## Proto

`workspace/proto/telemetry.proto` 是唯一协议源文件。

- Sidecar 构建执行 `protoc --cpp_out`。
- Web 构建执行 `protoc --go_out`。
- 生成代码不手写、不提交。
- C++ 使用 protobuf lite runtime，并静态链接到 Sidecar。

协议只传节点、链路、序号、采样时间、容量、已用空间、读写位置和 shutdown。
使用率由前端计算。

## 配置

Sidecar Exporter：

- `NODE_ID`，默认 `local`
- `TELEMETRY_HOST`，默认 `telemetry-web`
- `TELEMETRY_PORT`，默认 `9900`
- `SAMPLE_INTERVAL`，毫秒，默认 `100`

Web：

- `TELEMETRY_UDP_ADDR`，默认 `:9900`
- `TELEMETRY_HTTP_ADDR`，默认 `:8080`
- `HISTORY_SIZE`，默认每条链路保留 `600` 点

## 部署与故障隔离

- 每节点部署 Sidecar 和 Worker；集群只部署一个中央 Web。
- Web 不加入 Sidecar IPC namespace，也不挂载 Shared Memory。
- Exporter DNS、网络或 Proto异常由非关键进程承担，失败后1秒重启。
- Sidecar 主数据进程不等待 Telemetry、不读取 Telemetry 状态。
- Web 只使用有界内存，不依赖数据库、消息队列或反向 RPC。

Telemetry 仍消耗少量 CPU 和内存带宽，但不参与主链路同步，不形成网络反压。
