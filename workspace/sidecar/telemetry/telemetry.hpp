#pragma once

#include <csignal>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace sidecar::telemetry {

struct RingSnapshot {
    std::uint64_t capacity{};
    std::uint64_t used{};
    std::uint64_t write_position{};
    std::uint64_t read_position{};
    bool shutdown{};
};

using SnapshotCallback = std::function<bool(RingSnapshot&)>;

struct TelemetryTarget {
    std::string link_id;
    SnapshotCallback fetch_snapshot;
};

[[nodiscard]] int run_telemetry_exporter(
    volatile std::sig_atomic_t& running,
    const std::vector<TelemetryTarget>& targets);

}  // namespace sidecar::telemetry
