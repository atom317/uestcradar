#pragma once

#include <csignal>

struct RingBuffer;

namespace sidecar::tools {

// Test-only network stand-ins. They neither create nor map shared memory:
// main.cpp owns both RingBuffers and injects them into these loops.
//
// The source represents data received from a remote peer and therefore writes
// the worker-facing upstream ring. The sink represents a remote peer accepting
// worker output and therefore reads the worker-facing downstream ring.
void run_jitter_data_source(
    volatile std::sig_atomic_t& running,
    RingBuffer* upstream) noexcept;
void run_jitter_data_sink(
    volatile std::sig_atomic_t& running,
    RingBuffer* downstream) noexcept;

}  // namespace sidecar::tools
