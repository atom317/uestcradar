#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
compose_file="$root/workspace/sidecar/tools/compose.e2e-benchmark.yaml"
project="${COMPOSE_PROJECT_NAME:-uestcradar-e2e}"
repetitions="${REPETITIONS:-3}"
warmup="${WARMUP_SECONDS:-3}"

compose=(docker compose --project-name "$project" -f "$compose_file")

cleanup() {
    "${compose[@]}" down --volumes --remove-orphans >/dev/null 2>&1 || true
}
trap cleanup EXIT INT TERM

"${compose[@]}" build

for ((repetition = 1; repetition <= repetitions; ++repetition)); do
    samples="$(mktemp)"
    stop_file="$(mktemp)"
    rm -f "$stop_file"

    "${compose[@]}" up --detach --force-recreate
    sidecar_a="$("${compose[@]}" ps --all --quiet sidecar-a)"
    sidecar_b="$("${compose[@]}" ps --all --quiet sidecar-b)"
    worker_a="$("${compose[@]}" ps --all --quiet worker-a)"
    worker_b="$("${compose[@]}" ps --all --quiet worker-b)"

    (
        sleep "$warmup"
        while [[ ! -e "$stop_file" ]]; do
            for entry in "sidecar-a:$sidecar_a" "sidecar-b:$sidecar_b"; do
                service="${entry%%:*}"
                container="${entry#*:}"
                cpu="$(docker stats --no-stream \
                    --format '{{.CPUPerc}}' "$container" 2>/dev/null \
                    | tr -d '%')"
                if [[ -n "$cpu" ]]; then
                    printf '%s %s\n' "$service" "$cpu" >>"$samples"
                fi
            done
            sleep 1
        done
    ) &
    sampler=$!

    mapfile -t worker_statuses < <(
        docker wait "$worker_a" "$worker_b")
    touch "$stop_file"
    wait "$sampler"

    docker logs "$worker_a"
    docker logs "$worker_b"
    awk -v repetition="$repetition" '
        { total[$1] += $2; count[$1]++ }
        END {
            for (service in total) {
                printf "{\"benchmark\":\"sidecar-cpu\",\"repetition\":%d,\"service\":\"%s\",\"cpu_pct_mean\":%.3f,\"samples\":%d}\n",
                    repetition, service,
                    total[service] / count[service], count[service]
            }
        }
    ' "$samples"

    rm -f "$samples" "$stop_file"
    "${compose[@]}" down --volumes --remove-orphans
    for status in "${worker_statuses[@]}"; do
        if [[ "$status" != "0" ]]; then
            echo "e2e benchmark worker exited with status $status" >&2
            exit 1
        fi
    done
done
