#pragma once

#include <csignal>
#include <cstddef>

struct RingBuffer;

namespace sidecar::network {
class UCXMemoryRegion;
class UCXTransport;
}  // namespace sidecar::network

namespace sidecar::forwarder {

inline constexpr std::size_t kDefaultMaxTransferBytes = 256 * 1024;

struct ForwarderOptions {
    std::size_t max_transfer_bytes{kDefaultMaxTransferBytes};
};

void run_forwarder(
    volatile std::sig_atomic_t& running,
    RingBuffer* upstream,
    RingBuffer* downstream,
    network::UCXTransport& transport,
    const network::UCXMemoryRegion& upstream_memory,
    const network::UCXMemoryRegion& downstream_memory,
    const ForwarderOptions& options = {});

}  // namespace sidecar::forwarder
