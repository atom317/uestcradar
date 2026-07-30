# Common RingBuffer ABI

基于 POSIX shared memory 的无锁 SPSC 字节环。

这是 Sidecar 与 SDK 之间的公共 ABI 契约；它不属于任何一方的私有目录。它可被独立 CMake 配置为静态库 `uestcradar::ringbuf`。SDK 发布树携带同版本的 vendor 快照，以保证 SDK 单独交付时仍可自闭环编译。

- 生产者仅推进 `write_position`；消费者仅推进 `read_position`。
- 写满时 `ringbuf_write` 返回 `0`，不覆盖未消费数据。
- 位置字段分别独占缓存行；发布和读取通过 acquire/release 原子操作同步。
- `ringbuf_peek_read/commit_read` 和
  `ringbuf_reserve_write/commit_write` 暴露零拷贝连续区；返回区域最多到
  环尾，跨尾部分必须在下一次调用处理。
- 容量由创建方通过 `ringbuf_create(name, capacity_bytes)` 在运行时指定。
- 共享内存由固定 4096 字节控制头和连续字节数据区组成；头部只保存容量、读写位置、关闭标志和 ABI 信息，数据区不定义消息格式。
- `benchmark.cpp` 通过两个进程重新映射同一共享内存，并校验数据顺序。

运行基准：`make -C workspace/common/ringbuf test`。
