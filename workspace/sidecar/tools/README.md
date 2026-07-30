# Forwarder end-to-end benchmark

`network_benchmark.cpp` is an external UCX peer for the complete
benchmark → Sidecar → SDK Worker → Sidecar → benchmark byte-stream path.
The default jitter profile changes producer and consumer limits every three
seconds so RingBuffer backpressure is visible in telemetry.

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
