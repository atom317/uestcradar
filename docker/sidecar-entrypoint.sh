#!/usr/bin/env bash

set -u
set -m

producer_pid=
consumer_pid=
exporter_pid=

terminate() {
    trap - EXIT INT TERM
    for pid in "$producer_pid" "$consumer_pid" "$exporter_pid"; do
        if [[ -n "$pid" ]]; then
            kill -TERM -- "-$pid" 2>/dev/null || true
        fi
    done
    wait 2>/dev/null || true
}

trap terminate EXIT INT TERM

/app/sidecar produce-upstream &
producer_pid=$!

/app/sidecar consume-downstream &
consumer_pid=$!

(
    while true; do
        /app/sidecar export-telemetry || true
        sleep 1
    done
) &
exporter_pid=$!

wait -n "$producer_pid" "$consumer_pid"
status=$?
terminate
exit "$status"
