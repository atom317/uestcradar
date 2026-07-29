#include "tools/jitter_io.hpp"

#include "ringbuf/ringbuf.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <thread>

namespace sidecar::tools {
namespace {

struct JitterPhase {
    std::size_t chunk_size;
    std::chrono::milliseconds interval;
};

constexpr std::array<JitterPhase, 4> kJitterPhases{{
    {64 * 1024, std::chrono::milliseconds{2}},
    {32 * 1024, std::chrono::milliseconds{12}},
    {8 * 1024, std::chrono::milliseconds{70}},
    {48 * 1024, std::chrono::milliseconds{25}},
}};

const JitterPhase& current_phase(
    std::chrono::steady_clock::time_point started,
    std::size_t phase_offset) noexcept {
    const auto elapsed = std::chrono::steady_clock::now() - started;
    const auto phase_number = static_cast<std::size_t>(
        std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() /
        3);
    return kJitterPhases[
        (phase_number + phase_offset) % kJitterPhases.size()];
}

}  // namespace

void run_jitter_data_source(
    volatile std::sig_atomic_t& running,
    RingBuffer* upstream) noexcept {
    std::array<std::byte, 64 * 1024> bytes{};
    std::uint64_t sequence = 0;
    const auto started = std::chrono::steady_clock::now();

    while (running != 0 && !ringbuf_is_shutdown(upstream)) {
        const JitterPhase& phase = current_phase(started, 0);
        std::memcpy(bytes.data(), &sequence, sizeof(sequence));
        const std::int32_t written = ringbuf_write(
            upstream, bytes.data(), phase.chunk_size);
        if (written < 0) {
            running = 0;
            return;
        }
        ++sequence;
        std::this_thread::sleep_for(phase.interval);
    }
}

void run_jitter_data_sink(
    volatile std::sig_atomic_t& running,
    RingBuffer* downstream) noexcept {
    std::array<std::byte, 64 * 1024> bytes{};
    const auto started = std::chrono::steady_clock::now();

    while (running != 0 && !ringbuf_is_shutdown(downstream)) {
        const JitterPhase& phase = current_phase(started, 2);
        const std::int32_t read = ringbuf_read(
            downstream, bytes.data(), phase.chunk_size);
        if (read < 0) {
            running = 0;
            return;
        }
        std::this_thread::sleep_for(phase.interval);
    }
}

}  // namespace sidecar::tools
