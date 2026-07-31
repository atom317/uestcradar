#pragma once

#include <csignal>

struct RingBuffer;

namespace sidecar::network {
class UCXMemoryRegion;
class UCXTransport;
}  // namespace sidecar::network

namespace sidecar::forwarder {

void run_forwarder(
    volatile std::sig_atomic_t& running,
    RingBuffer* upstream,
    RingBuffer* downstream,
    network::UCXTransport& transport,
    const network::UCXMemoryRegion& upstream_memory,
    const network::UCXMemoryRegion& downstream_memory);

}  // namespace sidecar::forwarder
