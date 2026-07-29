ARG BASE_IMAGE=registry.chengyistudio.com/cxx/ubuntu:24.04

FROM ${BASE_IMAGE} AS builder

ENV DEBIAN_FRONTEND=noninteractive
RUN unset HTTP_PROXY HTTPS_PROXY http_proxy https_proxy ALL_PROXY all_proxy \
    && apt-get update \
    && apt-get install -y --no-install-recommends \
       g++ \
       make \
       qt5-qmake \
       qtbase5-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY workspace/qt5core/ .

WORKDIR /build
RUN /usr/lib/qt5/bin/qmake /src/qt5core_thread_example.pro CONFIG+=release \
    && make -j"$(nproc)"

FROM ${BASE_IMAGE} AS runtime

ENV DEBIAN_FRONTEND=noninteractive
RUN unset HTTP_PROXY HTTPS_PROXY http_proxy https_proxy ALL_PROXY all_proxy \
    && apt-get update \
    && apt-get install -y --no-install-recommends libqt5core5t64 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=builder /build/qt5core_thread_example /app/qt5core_thread_example

RUN ldd /app/qt5core_thread_example \
    && ! ldd /app/qt5core_thread_example | grep -q "not found"

ENTRYPOINT ["/app/qt5core_thread_example"]
