# syntax=docker/dockerfile:1
FROM registry.chengyistudio.com/cxx/algo-base:latest

WORKDIR /app
COPY src/ ./src/

# 编译算法业务代码
RUN cmake -S src -B build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build --parallel \
    && cp build/algorithm ./algorithm \
    && rm -rf src build

ENTRYPOINT ["/app/algorithm"]
