# Forwarder

Forwarder 是唯一同时使用 RingBuffer 数据 API 和 UCXTransport 数据 API
的应用层模块。它不创建共享内存、endpoint 或注册内存，也不理解业务数据。

## 数据路径

- outbound：从 worker-facing downstream 等待对端 credit，
  `ringbuf_acquire()` 持有一条完整记录，UCX Send 完成后才
  `ringbuf_release()`。
- inbound：在 worker-facing upstream 上调用 `ringbuf_reserve()` 持有一个
  完整 Slot，先投递 UCX Receive，再向对端发送该 Slot 的 Payload 容量；
  Receive 成功且长度合法后才 `ringbuf_commit()`，失败则 cancel。
- 建连后先交换两个端口的 `type_id/type_version/max_payload_bytes`。本机输出
  必须匹配远端输入，反方向同理；不匹配时在搬运 Payload 前失败。
- 每个方向最多一个 credit 和一个 Payload 请求在途。credit 是固定 8 字节
  网络字节序控制消息，Payload 不经过临时数组、vector 或 memcpy。
- 空环、满环和网络无进展时采用短自旋、yield、50 us 休眠的渐进退避。

## 零拷贝边界

Forwarder 接收由组合根创建的 `UCXMemoryRegion`。functional/TCP 模式只验证
逻辑正确性；strict-rdma 模式还需要 RC 设备、预注册成功、unlimited
memlock 和 rendezvous/get_zcopy 才能声明 Payload 使用 NIC DMA。

运行回归：

```bash
cmake -S workspace/sidecar -B build/sidecar -DBUILD_TESTING=ON
cmake --build build/sidecar --parallel
ctest --test-dir build/sidecar --output-on-failure
```
