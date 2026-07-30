# syntax=docker/dockerfile:1

ARG BASE_IMAGE=registry.chengyistudio.com/cxx/ubuntu:24.04

FROM ${BASE_IMAGE} AS builder

ENV DEBIAN_FRONTEND=noninteractive
RUN unset HTTP_PROXY HTTPS_PROXY http_proxy https_proxy ALL_PROXY all_proxy \
    && apt-get update \
    && apt-get install -y --no-install-recommends g++ \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY workspace/helloworld/main.cpp ./hello.cpp

RUN mkdir -p /out \
    && g++ -std=c++17 -O2 -Wall -Wextra \
       -static-libgcc -static-libstdc++ \
       hello.cpp -o /out/hello

FROM ${BASE_IMAGE} AS runtime

WORKDIR /app
COPY --from=builder /out/hello /app/hello

ENTRYPOINT ["/app/hello"]
