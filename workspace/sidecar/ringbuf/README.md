# RingBuffer

基于 POSIX shared memory 的无锁 SPSC 字节环。

- 生产者仅推进 `write_position`；消费者仅推进 `read_position`。
- 写满时 `ringbuf_write` 返回 `0`，不覆盖未消费数据。
- 位置字段分别独占缓存行；发布和读取通过 acquire/release 原子操作同步。
- `benchmark.cpp` 通过两个进程重新映射同一共享内存，并校验数据顺序。

运行基准：`make -C workspace/sidecar/ringbuf test`。
