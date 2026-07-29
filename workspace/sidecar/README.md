# Sidecar 通信代理架构设计规范 (Sidecar Architecture)

> 架构原则：**依赖解耦 (Dependency Decoupling)** 与 **组合根模式 (Composition Root Pattern)**
>
> 核心目标：模块物理解耦、接口最窄化、控制逻辑统一由 `main.cpp` 组装，支持多 Sidecar 跨节点级联。

---

## 1. 物理架构拓扑与多 Sidecar 协同关系

在分布式流水线集群中，**每台物理节点/容器独立运行一个 Sidecar 通信代理**。多个 Sidecar 之间通过极简网络模块 (`network/`) 构成点对点的串行数据传输链路：

```text
 节点 1 (Node A)                                            节点 2 (Node B)
┌──────────────────────────────────────┐                   ┌──────────────────────────────────────┐
│ 算法进程 A (Worker A)                 │                   │ 算法进程 B (Worker B)                 │
└──────────────────┬───────────────────┘                   └──────────────────▲───────────────────┘
                   │ POSIX Shm (/upstreambuf)                                 │ POSIX Shm (/downstreambuf)
┌──────────────────▼───────────────────┐                   ┌──────────────────┴───────────────────┐
│ Sidecar 代理 A (Agent A)             │                   │ Sidecar 代理 B (Agent B)             │
│                                      │                   │                                      │
│  [ringbuf 模块]                      │                   │  [ringbuf 模块]                      │
│        │ (零拷贝读取)                 │                   │        ▲ (零拷贝写入)                │
│        ▼                             │                   │        │                             │
│  [forwarder 模块]                    │                   │  [forwarder 模块]                    │
│   (死循环搬运 / 依赖注入)             │                   │   (死循环搬运 / 依赖注入)             │
│        │                             │                   │        ▲                             │
│        ▼                             │                   │        │                             │
│  [network 模块]                      │   物理 RDMA/UCX   │  [network 模块]                      │
│   (UCP Tag Send) ────────────────────┼───────────────────┼───► (UCP Tag Recv)                   │
└──────────────────┬───────────────────┘                   └──────────────────┬───────────────────┘
                   │ 100ms 单向 UDP                                           │ 100ms 单向 UDP
                   ▼                                                          ▼
┌─────────────────────────────────────────────────────────────────────────────────────────────────┐
│                              中央监控服务 (telemetry-web 容器)                                  │
└─────────────────────────────────────────────────────────────────────────────────────────────────┘
```

---

## 2. 多个 Sidecar 代理之间的物理关系与协同规则

多个 Sidecar 代理之间（例如 Sidecar A ➔ Sidecar B ➔ Sidecar C）遵循以下纯粹的物理协同规则：

1. **点对点对等关系 (Peer-to-Peer)**：
   - 各节点 Sidecar 物理地位对等，不设立主从 (Master-Slave) 节点；
   - 依赖静态配置（如 `PEER_HOST`, `PEER_PORT`）建立专用的点对点 RDMA/UCX 通信通道。
2. **单向物理数据管道 (Unidirectional Data Pipe)**：
   - 上游 Sidecar A 从本机的 `/upstreambuf` 零拷贝读取数据，通过 `forwarder` 泵入 `network` 模块打入网络；
   - 下游 Sidecar B 的 `network` 模块从网络接收字节流，通过 `forwarder` 泵出并直接落盘写入本机的 `/downstreambuf` 给下游算法消费。
3. **解耦的旁路监控拓扑 (Decoupled Telemetry)**：
   - 多个 Sidecar 分别独立向中央 `telemetry-web` 节点点对点发送 100ms UDP 监控快照；
   - 中央监控节点仅为只读观察者，Sidecar 之间的主通信链路完全脱离监控节点的干预。

---

## 3. 内部模块职责与物理解耦边界

系统的物理指挥官 `main.cpp`（组合根 Composition Root）负责在启动时统一实例化并组装各模块。当前里程碑只启用 `ringbuf` 与 `telemetry`；`network` 和 `forwarder` 保留为后续接入点，不在主循环中伪造搬运逻辑。

```text
                                  main.cpp (组合根 Composition Root)
                              ┌──────────────────────────────────────┐
                              │ - 读取环境变量与节点参数            │
                              │ - 实例化并依赖注入 (DI) 各独立模块   │
                              │ - 统一捕获信号 (SIGINT/SIGTERM)      │
                              └──────┬───────────┬───────────┬───────┘
                                     │           │           │
                   ┌─────────────────┘           │           └─────────────────┐
                   │ (后台旁路运行)              │ (句柄与资源注入)            │ (句柄与资源注入)
                   ▼                             ▼                             ▼
   ┌───────────────────────────────┐ ┌───────────────────────────────┐ ┌───────────────────────────────┐
   │  telemetry 模块 (旁路导出)    │ │    forwarder 模块 (数据泵)    │ │   底层基础设施 (Ring/Net)     │
   ├───────────────────────────────┤ ├───────────────────────────────┤ ├───────────────────────────────┤
   │ - 纯粹负责 100ms UDP 指标导出 │ │ - 纯粹负责 while 循环高性能搬运│ │ - ringbuf: 纯处理 POSIX Shm │
   │ - 绝对不持有用主 Data Buffer  │ │ - 专注 CPU 绑核与背压调优     │ │ - network: 纯处理原生 UCX   │
   │ - 彻底不感知网络/Shm 实现     │ │ - 负责缝合桥接 Ring 与 Net 接口│ │ - 此二者互相绝对物理隔离隔离 │
   └───────────────────────────────┘ └───────────────────────────────┘ └───────────────────────────────┘
```

### 3.1 `common/ringbuf/` 模块（本机 Shm 数据面基础设施）

* **物理使命**：提供共享内存缓冲区的创建、映射与无锁零拷贝读写；
* **解耦规则**：纯粹处理本机内存，100% 不感知网络模块、Telemetry 模块以及 Forwarder 的存在。

### 3.2 `network/` 模块（跨机物理传输通道 基础设施）

* **物理使命**：封装最基础的 UCX (UCP) / libibverbs 原生底层 API（`ucp_init`, `ucp_tag_send_nbx` 等）；
* **解耦规则**：仅专注于字节流在物理网卡间的搬运，不理解 Payload 业务含义，不与 `ringbuf` 产生交叉依赖。

### 3.3 `telemetry/` 模块（旁路观察者）

* **物理使命**：定时（100ms）调用 `main.cpp` 注入的快照回调，打包非阻塞 UDP 发往中央 Web 监控节点；
* **解耦规则**：不引用 `ringbuf`，不调用共享内存 API。崩溃或丢包绝不回压主数据链路，由 `main.cpp` 拉起为后台旁路线程。

### 3.4 `forwarder/` 模块（转发引擎/数据泵）

* **物理使命**：充当高速公路的“搬运工”，将 `ringbuf` 与 `network` 的读写 API 无缝桥接在一起，在极其紧凑的死循环中极限压榨 CPU 完成搬运。
* **解耦规则**：遵循奥卡姆剃刀，不负责底层资源生命周期的创建与销毁（如不由它去调 `shm_open`），所有资源通过 `main.cpp` 注入。只做盲目的字节搬运，坚决不含业务解析。

### 3.5 `main.cpp`（物理组合根 Composition Root）

* **物理使命**：全局唯一的物理控制中心，负责解析配置、创建底层句柄，然后将句柄注入给 `forwarder` 和 `telemetry`；全盘接管进程生命周期。
* **解耦红线**：严禁在此堆砌任何雷达业务的模拟数据生产与消费逻辑（Mock Worker 逻辑应当拆分到独立的外部工具中）。

---

## 4. 目录与物理文件结构

```text
workspace/sidecar/
├── README.md               # 本架构设计规范文档
├── main.cpp                # 唯一组合根：配置、资源组装与主循环
├── telemetry/              # 监控
│   ├── README.md           # 监控模块极简设计规范
│   ├── telemetry.hpp
│   └── telemetry.cpp
├── network/                # 极简 UCX / RDMA 物理网络搬运模块
│   ├── README.md           # 网络模块极简设计规范
│   ├── ucx_transport.hpp   # UCXTransport 声明
│   ├── ucx_transport.cpp   # 最基础 UCX/UCP API 调用
│   ├── transport_test.cpp  # 独立功能测试
│   └── benchmark.cpp       # 双端吞吐与 RTT 基线
└── forwarder/              # 数据转发引擎/搬运工
    ├── README.md           # 转发引擎模块设计规范（奥卡姆剃刀法则）
    ├── forwarder.hpp       # 引擎接口与状态声明
    └── forwarder.cpp       # 引擎高性能死循环实现
```

`ringbuf` 的物理位置为 `workspace/common/ringbuf/`，它是 Sidecar 与 SDK 共享的 ABI 契约，不属于任一方的私有实现。

---

## 5. 架构约束与红线 (Architecture Rules)

1. **零横向交叉依赖红线**：基础设施层的 `ringbuf` 与 `network` 之间**绝对禁止出现任何形式的头文件互相 `#include`**；
2. **严格层级规则**：仅允许处于应用调度层的 `forwarder` 和 `main.cpp` 引用基础设施层的头文件（允许向名单向依赖）。
3. **极致轻量红线**：网络模块只允许封装最底层的 UCX 原生 API，绝不引入复杂的 RPC 框架或动态路由表。
4. **控制反转 (IoC)**：所有底层资源建立与销毁，必须统一发生于 `main.cpp`（组合根）中，严禁在 `forwarder` 中私下创建共享内存或网卡端点。

---

## 6. 当前启动模型

`sidecar` 不接收模式参数。进程启动后总是创建 `/upstreambuf` 和 `/downstreambuf` 两个 SPSC 字节环，并在后台启动 telemetry。两个环使用相同的 `RING_CAPACITY_BYTES`，未配置时默认为 1 MiB。

测试数据生产和消费位于独立的 `workspace/tools/mock_worker.cpp`，不属于 Sidecar。Compose 为每个节点启动一个 Sidecar 和一个共享其 IPC namespace 的 mock worker：

```bash
ALPHA_RING_CAPACITY_BYTES=8388608 \
BETA_RING_CAPACITY_BYTES=16777216 \
docker compose up --build
```

`shm_size` 必须大于两个数据区与两个 4096 字节控制头的总和，建议保留额外余量。当前尚未接入 network/forwarder，因此 mock worker 写入的 upstream 会在填满后体现背压，downstream 在没有外部写入时保持为空；Sidecar 不做隐式本地回环。
