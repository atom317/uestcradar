#include "ringbuf/ringbuf.hpp"

#include <array>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

namespace {

volatile std::sig_atomic_t running = 1;

void stop(int) {
    running = 0;
}

void install_signal_handlers() {
    std::signal(SIGINT, stop);
    std::signal(SIGTERM, stop);
}

using RingHandle =
    std::unique_ptr<RingBuffer, decltype(&ringbuf_close)>;

struct ProductionPhase {
    std::size_t chunk_size;
    std::chrono::milliseconds interval;
    const char* label;
};

constexpr std::array<ProductionPhase, 4> kProductionPhases{{
    {64 * 1024, std::chrono::milliseconds{2}, "burst"},
    {32 * 1024, std::chrono::milliseconds{12}, "fast"},
    {8 * 1024, std::chrono::milliseconds{70}, "quiet"},
    {48 * 1024, std::chrono::milliseconds{25}, "recover"},
}};

void produce(RingBuffer* upstream) {
    std::vector<std::byte> chunk(64 * 1024);
    std::uint64_t sequence = 0;
    std::uint64_t interval_bytes = 0;
    auto phase_started = std::chrono::steady_clock::now();
    auto last_report = phase_started;
    std::size_t phase_index = 0;

    while (running != 0 && !ringbuf_is_shutdown(upstream)) {
        const auto now = std::chrono::steady_clock::now();
        if (now - phase_started >= std::chrono::seconds{3}) {
            phase_index = (phase_index + 1) % kProductionPhases.size();
            phase_started = now;
        }
        const ProductionPhase& phase =
            kProductionPhases[phase_index];

        std::memcpy(chunk.data(), &sequence, sizeof(sequence));
        const std::int32_t result = ringbuf_write(
            upstream,
            chunk.data(),
            phase.chunk_size);
        if (result < 0) {
            std::cerr << "mock-worker: upstream write failed\n";
            running = 0;
            break;
        }
        interval_bytes += static_cast<std::uint64_t>(result);
        ++sequence;

        if (now - last_report >= std::chrono::seconds{1}) {
            std::cout
                << "mock-worker: producer phase=" << phase.label
                << " accepted=" << interval_bytes
                << " B/s" << std::endl;
            interval_bytes = 0;
            last_report = now;
        }
        std::this_thread::sleep_for(phase.interval);
    }
}

void consume(RingBuffer* downstream) {
    std::vector<std::byte> chunk(64 * 1024);
    std::uint64_t interval_bytes = 0;
    auto last_report = std::chrono::steady_clock::now();

    while (running != 0 && !ringbuf_is_shutdown(downstream)) {
        const std::int32_t result = ringbuf_read(
            downstream,
            chunk.data(),
            chunk.size());
        if (result < 0) {
            std::cerr << "mock-worker: downstream read failed\n";
            running = 0;
            break;
        }
        interval_bytes += static_cast<std::uint64_t>(result);

        const auto now = std::chrono::steady_clock::now();
        if (now - last_report >= std::chrono::seconds{1}) {
            std::cout
                << "mock-worker: consumer received="
                << interval_bytes << " B/s" << std::endl;
            interval_bytes = 0;
            last_report = now;
        }
        if (result == 0) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds{5});
        }
    }
}

}  // namespace

int main() {
    install_signal_handlers();

    try {
        RingHandle upstream{
            ringbuf_open(kUpstreamBufName),
            &ringbuf_close,
        };
        RingHandle downstream{
            ringbuf_open(kDownstreamBufName),
            &ringbuf_close,
        };

        std::cout
            << "mock-worker: connected, upstream="
            << ringbuf_capacity(upstream.get())
            << " bytes, downstream="
            << ringbuf_capacity(downstream.get())
            << " bytes" << std::endl;

        std::thread producer{produce, upstream.get()};
        std::thread consumer{consume, downstream.get()};
        producer.join();
        consumer.join();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "mock-worker: " << error.what() << '\n';
        return 1;
    }
}
