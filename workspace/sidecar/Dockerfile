# syntax=docker/dockerfile:1

ARG BASE_IMAGE=registry.chengyistudio.com/cxx/ubuntu:24.04

FROM ${BASE_IMAGE} AS builder

ENV DEBIAN_FRONTEND=noninteractive
RUN which g++ >/dev/null 2>&1 && which cmake >/dev/null 2>&1 && which ninja >/dev/null 2>&1 || ( \
    unset HTTP_PROXY HTTPS_PROXY http_proxy https_proxy ALL_PROXY all_proxy \
    && apt-get update \
    && apt-get install -y --no-install-recommends \
       cmake g++ libprotobuf-dev libucx-dev ninja-build pkg-config \
       protobuf-compiler \
    && rm -rf /var/lib/apt/lists/* )

WORKDIR /src
COPY workspace/common/ringbuf/ ./common/ringbuf/
COPY workspace/sidecar/ ./sidecar/
COPY workspace/sdk/ ./sdk/
COPY workspace/proto/telemetry.proto ./proto/telemetry.proto
RUN mkdir -p /out \
    && cmake -S sidecar -B sidecar-build -G Ninja \
       -DCMAKE_BUILD_TYPE=Release \
       -DBUILD_TESTING=OFF \
    && cmake --build sidecar-build --parallel --target sidecar \
    && cmake -S sdk -B sdk-build -G Ninja \
       -DCMAKE_BUILD_TYPE=Release \
       -DBUILD_TESTING=OFF \
    && cmake --build sdk-build --parallel --target uestcradar_sdk \
    && g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic \
       -I/src/sdk/include \
       sidecar/tools/e2e_benchmark.cpp \
       -L/src/sdk-build -Wl,-rpath,/app \
       -luestcradar_sdk \
       -o /out/e2e-benchmark \
    && cp sidecar-build/sidecar /out/sidecar

FROM ${BASE_IMAGE} AS network-builder

ENV DEBIAN_FRONTEND=noninteractive
RUN which g++ >/dev/null 2>&1 && which cmake >/dev/null 2>&1 && which ninja >/dev/null 2>&1 || ( \
    unset HTTP_PROXY HTTPS_PROXY http_proxy https_proxy ALL_PROXY all_proxy \
    && apt-get update \
    && apt-get install -y --no-install-recommends \
       cmake g++ libucx-dev ninja-build pkg-config \
    && rm -rf /var/lib/apt/lists/* )

WORKDIR /src/sidecar
COPY workspace/sidecar/network/ ./network/
COPY workspace/sidecar/forwarder/forwarder_protocol.hpp ./forwarder/forwarder_protocol.hpp
COPY workspace/sidecar/tools/network_benchmark.cpp ./tools/network_benchmark.cpp
RUN cmake -S network -B network/build -G Ninja \
       -DCMAKE_BUILD_TYPE=Release \
       -DBUILD_TESTING=ON \
    && cmake --build network/build --parallel \
    && ctest --test-dir network/build --output-on-failure

FROM ${BASE_IMAGE} AS network-benchmark

ENV DEBIAN_FRONTEND=noninteractive
RUN unset HTTP_PROXY HTTPS_PROXY http_proxy https_proxy ALL_PROXY all_proxy \
    && apt-get update \
    && apt-get install -y --no-install-recommends libucx0 ucx-utils \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=network-builder \
    /src/sidecar/network/build/ucx-network-benchmark \
    /app/ucx-network-benchmark

ENTRYPOINT ["/app/ucx-network-benchmark"]

FROM ${BASE_IMAGE} AS forwarder-benchmark

ENV DEBIAN_FRONTEND=noninteractive
RUN unset HTTP_PROXY HTTPS_PROXY http_proxy https_proxy ALL_PROXY all_proxy \
    && apt-get update \
    && apt-get install -y --no-install-recommends libucx0 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=network-builder \
    /src/sidecar/network/build/sidecar-network-benchmark \
    /app/network-benchmark

ENTRYPOINT ["/app/network-benchmark"]

FROM ${BASE_IMAGE} AS runtime

ENV DEBIAN_FRONTEND=noninteractive
RUN unset HTTP_PROXY HTTPS_PROXY http_proxy https_proxy ALL_PROXY all_proxy \
    && apt-get update \
    && apt-get install -y --no-install-recommends libucx0 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=builder /out/sidecar /app/sidecar

ENTRYPOINT ["/app/sidecar"]

FROM ${BASE_IMAGE} AS e2e-benchmark

WORKDIR /app
COPY --from=builder /out/e2e-benchmark /app/e2e-benchmark
COPY --from=builder /src/sdk-build/libuestcradar_sdk.so* /app/

ENTRYPOINT ["/app/e2e-benchmark"]
