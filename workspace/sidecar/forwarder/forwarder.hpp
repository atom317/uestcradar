#pragma once

#include <csignal>
#include <cstddef>

struct RingBuffer;

namespace sidecar::network {
class UCXMemoryRegion;
class UCXTransport;
}  // namespace sidecar::network

namespace sidecar::forwarder {

struct DroppedFrames {
    std::size_t frames{0};
    std::size_t bytes{0};
};

void run_ingress_session(
    volatile std::sig_atomic_t& running,
    RingBuffer* input,
    network::UCXTransport& transport,
    const network::UCXMemoryRegion& input_memory);

void run_egress_session(
    volatile std::sig_atomic_t& running,
    RingBuffer* output,
    network::UCXTransport& transport,
    const network::UCXMemoryRegion& output_memory);

[[nodiscard]] DroppedFrames drop_stale_frames(
    RingBuffer* output) noexcept;

}  // namespace sidecar::forwarder
