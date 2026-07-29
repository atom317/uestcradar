# syntax=docker/dockerfile:1

FROM golang:1.24-bookworm AS builder

RUN apt-get update \
    && apt-get install -y --no-install-recommends protobuf-compiler \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src/web
COPY workspace/web/go.mod workspace/web/go.sum ./
RUN go mod download \
    && go install google.golang.org/protobuf/cmd/protoc-gen-go@v1.36.6

COPY workspace/web/ ./
COPY workspace/proto/telemetry.proto /src/proto/telemetry.proto
RUN protoc \
      -I /src/proto \
      --go_out=. \
      --go_opt=module=uestcradar/telemetry \
      /src/proto/telemetry.proto \
    && gofmt -w ./cmd ./internal ./web \
    && go test -race ./... \
    && CGO_ENABLED=0 go build -buildvcs=false -trimpath -ldflags="-s -w" \
       -o /out/telemetry ./cmd/telemetry

FROM scratch

COPY --from=builder /out/telemetry /telemetry

ENTRYPOINT ["/telemetry"]
