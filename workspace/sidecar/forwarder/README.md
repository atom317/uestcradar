# Forwarder 转发引擎模块设计规范 (Data Pump)

> 架构设计原则：**奥卡姆剃刀原理（Occam's Razor: 如无必要，勿增实体）**
>
> 模块定位：**纯粹的数据搬运工（Data Pump），只专注于高性能、零拷贝的数据桥接，绝不包含业务逻辑，也不负责底层资源的创建与销毁。**

---

## 1. 物理职责与“剃刀法则”

本模块是整个 Sidecar 代理的数据主轴发动机，负责持续不断地将数据从共享内存（RingBuffer）泵入物理网卡（Network），或从网卡泵入共享内存。

### ✅ 核心职责 (Goals)
1. **死循环数据泵 (High-Performance Loop)**：在一个或多个紧凑的线程循环中，高速轮询 RingBuffer，并调用 `UCXTransport::send`（或 `receive` 配合 RingBuffer 写入）。
2. **状态机与背压 (Backpressure) 处理**：如果网卡发送队列满（网络拥塞），或者下游内存写满（算法处理慢），妥善处理 yield 礼让与重试机制，确保全链路零丢包。
3. **线程绑核与调优 (Thread Affinity)**：为保证极低延迟，本层适合封装 CPU 绑核（Set CPU Affinity）与繁忙轮询逻辑。

### ❌ 剃刀法则：坚决排除的越界职责 (Non-Goals)
* **绝对不负责底层资源生命周期**：绝不主动执行 `shm_open`、`shm_unlink` 或 `ucp_init`。所有的资源句柄（RingBuffer*、Endpoint* 等）必须由外部（`main.cpp` 组合根）在启动时通过**依赖注入（DI）**传入。
* **绝对不理解数据 Payload 内容**：对传输的雷达数据视作纯粹的无差别“裸字节流（Raw Bytes）”，绝不在此处做序列化、反序列化、包头解析或任何业务维度的干预。
* **绝对不处理全局信号**：不主动截获 `SIGINT`/`SIGTERM`，完全依靠 `main.cpp` 传入的 `volatile sig_atomic_t* running` 指针来安全退出循环。

---

## 2. 与其他模块的交互边界 (Cross-Boundary Rules)

`forwarder` 属于系统中的**应用调度层（Application / Coordinator Layer）**，它架设在基础设施层（RingBuf / Network）之上：

* **对上（面向 main.cpp）**：只暴露 `forwarder_run(...)` 或类似 `ForwarderEngine` 类的极简启动接口给 `main.cpp`。`main` 负责喂给它组装好的网络和内存句柄。
* **对下（面向底层组件）**：允许 `#include "ringbuf/ringbuf.hpp"` 和 `#include "network/ucx_transport.hpp"`。但它仅仅是这两个库的**使用者**，绝不能破坏这两大组件之间必须“物理绝缘”的红线。
