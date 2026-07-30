# Forwarder end-to-end benchmark

`network_benchmark.cpp` 是只测试 UCX Transport 的外部 peer。
`e2e_benchmark.cpp` 则通过正式 SDK，覆盖：

```text
Worker A → downstream Slot Ring → Sidecar A → UCX →
Sidecar B → upstream Slot Ring → Worker B
```

运行可重复的双 Sidecar 端到端测试：

```bash
PAYLOAD_BYTES=65536 SLOT_COUNT=64 \
WARMUP_SECONDS=3 DURATION_SECONDS=30 \
RATE_MIB_S=500 WAVE_PERIOD_SECONDS=4 \
REPETITIONS=3 \
workspace/sidecar/tools/run_e2e_benchmark.sh
```

`RATE_MIB_S=0` 表示不限速；非零时生产速率按周期波动，用于观察背压水位。
producer 和 consumer 分别输出 JSON，其中 consumer 包含有效带宽、消息率、
平均/P50/P99 单向延迟及 Worker CPU；运行脚本另以每秒一次的 `docker stats`
输出两个 Sidecar 的平均 CPU。正式结果应至少预热 3 秒、测量 30 秒并重复 3
次。原始 JSONL 应连同 CPU 型号、内核、UCX 版本、`UCX_TLS`、NUMA/绑核、
Payload 和 Slot 参数一并保存。Compose 内的容器共享宿主 monotonic clock；
跨机器测单向延迟前必须校时。

下面的旧工具用于单独诊断 Transport，不代表完整 SDK 链路。

Build the host tool:

```bash
cmake -S workspace/sidecar -B build/sidecar -DCMAKE_BUILD_TYPE=Release
cmake --build build/sidecar --target sidecar-network-benchmark --parallel
```

Start the Docker side of the functional TCP test:

```bash
BASE_IMAGE=ubuntu:24.04 \
docker compose \
  -f workspace/sidecar/tools/compose.network-benchmark.yaml \
  up --build
```

Then run the external peer:

```bash
build/sidecar/network/sidecar-network-benchmark \
  --host 127.0.0.1 \
  --port 13337 \
  --duration 24 \
  --profile jitter
```

Open `http://127.0.0.1:8080` to observe upstream and downstream water levels.
This TCP profile validates behavior only; it is not evidence of RDMA DMA
zero-copy.

For a two-host RDMA validation, start the Docker services on the server host
with the RDMA override:

```bash
docker compose \
  -f workspace/sidecar/tools/compose.network-benchmark.yaml \
  -f workspace/sidecar/tools/compose.network-benchmark.rdma.yaml \
  up --build
```

On the client host, grant sufficient memlock and run:

```bash
UCX_PROTO_INFO=y build/sidecar/network/sidecar-network-benchmark \
  --host <server-rdma-ip> --strict-rdma --profile jitter
```

The strict profile pins the two RingBuffer data mappings, limits UCX to RC,
and forces rendezvous/get_zcopy. Startup failure is expected when the RDMA
device, driver, route, or memlock allowance is unavailable.
