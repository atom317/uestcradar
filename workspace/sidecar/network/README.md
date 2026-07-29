# Sidecar 极简网络通信模块架构设计 (Network Component)

> 架构设计原则：**奥卡姆剃刀原理（Occam's Razor: 如无必要，勿增实体）**
>
> 模块定位：**只专注于提供最基础、最直接的物理 RDMA/UCX 通信，绝不引入冗余包装与中间层。**

---

## 1. 模块设计思想与物理职责

本 `network` 模块是 `cycomm-sidecar-agent` 跨机数据搬运的物理引擎。它仅承担一个物理职责：

$$ \text{本机 Shared Memory (/upstreambuf)} \xrightarrow{\text{网络传输通道 (network)}} \text{远端 Sidecar (/downstreambuf)} $$

### ❌ 剃刀法则：明确排除（Non-Goals）
为了保持极简与确定性的低延迟性能，本模块**绝对不承担**以下职责：
* **不实现** 复杂的应用层 RPC 框架；
* **不实现** 动态服务发现、路由表与 DAG 拓扑编排；
* **不实现** 冗余的多层抽象接口与继承树；
* **不实现** 业务 Payload 序列化与反序列化（仅做 Raw Binary 无损搬运）。

---

## 2. 最基础 UCX (UCP) 底层 API 映射关系

本模块纯粹基于开源 **UCX (Unified Communication X)** 的最基础原生 C API 实现，直接映射 4 大底层原语：

```text
┌──────────────────────────────────────────────────────────────────────────────┐
│ [初始化与建链]                                                               │
│ 1. ucp_init()       : 初始化 UCX 物理上下文与硬件资源 (IB/RoCE/TCP)        │
│ 2. ucp_ep_create()  : 根据目标机器地址 (IP:Port) 创建点对点通信 Endpoint      │
├──────────────────────────────────────────────────────────────────────────────┤
│ [极速数据收发 (Zero-Copy Tagged I/O)]                                        │
│ 3. ucp_tag_send_nbx(): 非阻塞极速 Tag 发送 Payload (可以直接从 Shm 缓冲区发)  │
│ 4. ucp_tag_recv_nbx(): 非阻塞极速 Tag 接收 Payload (直接落盘入本机的 Shm)    │
└──────────────────────────────────────────────────────────────────────────────┘
```

---

## 3. 极简网络桥接流程 (Network Data Path)

```text
             [本机 Shm /upstreambuf]
                        │
                        ▼  (1) 读取待发送数据帧 (Zero-Copy Read)
             ┌─────────────────────┐
             │ network::rdma_send  │  (2) 调用 ucp_tag_send_nbx() 原生接口
             └──────────┬──────────┘
                        │  RDMA (RoCE / IB / TCP 回退)
                        ▼
             ┌─────────────────────┐
             │ network::rdma_recv  │  (3) 调用 ucp_tag_recv_nbx() 原生接口
             └──────────┬──────────┘
                        │
                        ▼  (4) 直接落盘写入 (Zero-Copy Write)
            [远端 Shm /downstreambuf]
```

---

## 4. 后续物理文件划分规范

当启动代码实现时，`network` 目录下仅保留以下纯粹的实现文件：

* 📄 `network/rdma_transport.hpp`：定义原生 UCX Context / Endpoint 句柄与极简接口；
* 📄 `network/rdma_transport.cpp`：仅包含 4 大原生 UCX C API 的物理驱动调用；
* 📄 `network/README.md`：本架构设计说明文档。
