#include "ringbuf/ringbuf.hpp"
#include "telemetry/telemetry.hpp"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
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

std::size_t ring_capacity_from_environment() {
    const char* value = std::getenv("RING_CAPACITY_BYTES");
    if (value == nullptr || value[0] == '\0') {
        return kDefaultRingCapacity;
    }

    char* end = nullptr;
    errno = 0;
    const unsigned long long parsed =
        std::strtoull(value, &end, 10);
    if (errno != 0 || end == value || *end != '\0' ||
        parsed < 4096 ||
        parsed >
            std::numeric_limits<std::size_t>::max() -
                kRingHeaderSize) {
        throw std::invalid_argument(
            "RING_CAPACITY_BYTES must be an integer >= 4096");
    }
    return static_cast<std::size_t>(parsed);
}

class OwnedRing {
public:
    OwnedRing(const char* name, std::size_t capacity)
        : name_(name),
          ring_(ringbuf_create(name, capacity)) {}

    OwnedRing(const OwnedRing&) = delete;
    OwnedRing& operator=(const OwnedRing&) = delete;

    ~OwnedRing() {
        ringbuf_shutdown(ring_);
        ringbuf_close(ring_);
        ringbuf_unlink(name_);
    }

    [[nodiscard]] RingBuffer* get() const noexcept {
        return ring_;
    }

private:
    const char* name_;
    RingBuffer* ring_;
};

bool snapshot_ring(
    const RingBuffer* ring,
    sidecar::telemetry::RingSnapshot& output) noexcept {
    const std::uint64_t capacity =
        ring->header->capacity_bytes;
    for (int attempt = 0; attempt < 3; ++attempt) {
        const std::uint64_t read =
            ring->header->read_position.load(
                std::memory_order_acquire);
        const std::uint64_t write =
            ring->header->write_position.load(
                std::memory_order_acquire);
        if (write >= read && write - read <= capacity) {
            output = sidecar::telemetry::RingSnapshot{
                capacity,
                write - read,
                write,
                read,
                ring->header->shutdown.load(
                    std::memory_order_acquire) != 0,
            };
            return true;
        }
    }
    return false;
}

}  // namespace

int main() {
    install_signal_handlers();

    try {
        const std::size_t capacity =
            ring_capacity_from_environment();
        OwnedRing upstream{kUpstreamBufName, capacity};
        OwnedRing downstream{kDownstreamBufName, capacity};

        const std::vector<sidecar::telemetry::TelemetryTarget> targets{
            {
                "upstream",
                [ring = upstream.get()](
                    sidecar::telemetry::RingSnapshot& output) noexcept {
                    return snapshot_ring(ring, output);
                },
            },
            {
                "downstream",
                [ring = downstream.get()](
                    sidecar::telemetry::RingSnapshot& output) noexcept {
                    return snapshot_ring(ring, output);
                },
            },
        };

        std::exception_ptr telemetry_error;
        std::thread telemetry_thread([&] {
            try {
                if (sidecar::telemetry::run_telemetry_exporter(
                        running,
                        targets) != 0) {
                    throw std::runtime_error(
                        "telemetry exporter failed");
                }
            } catch (...) {
                telemetry_error = std::current_exception();
                running = 0;
            }
        });

        std::cout
            << "sidecar: upstream and downstream ready, capacity="
            << capacity << " bytes each" << std::endl;
        std::cout
            << "sidecar: network/forwarder disabled in this milestone"
            << std::endl;

        while (running != 0) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(100));
        }

        ringbuf_shutdown(upstream.get());
        ringbuf_shutdown(downstream.get());
        telemetry_thread.join();
        if (telemetry_error != nullptr) {
            std::rethrow_exception(telemetry_error);
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "sidecar: " << error.what() << '\n';
        return 1;
    }
}
