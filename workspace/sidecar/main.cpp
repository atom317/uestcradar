#include "forwarder/forwarder.hpp"
#include "network/ucx_transport.hpp"
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
#include <span>
#include <string>
#include <string_view>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

volatile std::sig_atomic_t running = 1;
constexpr std::uint64_t kDefaultFrameTypeId = 1;

void stop(int) {
    running = 0;
}

void install_signal_handlers() {
    std::signal(SIGINT, stop);
    std::signal(SIGTERM, stop);
}

std::size_t size_from_environment(
    const char* name,
    std::size_t fallback,
    std::size_t minimum,
    std::size_t maximum) {
    const char* value = std::getenv(name);
    if (value == nullptr || value[0] == '\0') {
        return fallback;
    }
    char* end = nullptr;
    errno = 0;
    const unsigned long long parsed =
        std::strtoull(value, &end, 10);
    if (errno != 0 || end == value || *end != '\0' ||
        parsed < minimum || parsed > maximum) {
        throw std::invalid_argument(
            std::string{name} + " is out of range");
    }
    return static_cast<std::size_t>(parsed);
}

std::string environment_or(
    const char* name,
    const char* fallback) {
    const char* value = std::getenv(name);
    return value == nullptr || value[0] == '\0'
               ? std::string{fallback}
               : std::string{value};
}

std::uint64_t uint64_from_environment(
    const char* name,
    std::uint64_t fallback) {
    const char* value = std::getenv(name);
    if (value == nullptr || value[0] == '\0') {
        return fallback;
    }
    char* end = nullptr;
    errno = 0;
    const unsigned long long parsed =
        std::strtoull(value, &end, 0);
    if (errno != 0 || end == value || *end != '\0' || parsed == 0) {
        throw std::invalid_argument(
            std::string{name} + " must be a positive integer");
    }
    return static_cast<std::uint64_t>(parsed);
}

RingBufferConfig ring_config_from_environment(
    const char* slot_count_name,
    const char* max_payload_name,
    const char* type_id_name,
    const char* type_version_name) {
    return {
        static_cast<std::uint32_t>(size_from_environment(
            slot_count_name, kDefaultSlotCount, 2, UINT32_MAX)),
        static_cast<std::uint32_t>(size_from_environment(
            max_payload_name,
            kDefaultMaxPayloadBytes,
            1,
            INT32_MAX)),
        uint64_from_environment(type_id_name, kDefaultFrameTypeId),
        static_cast<std::uint32_t>(size_from_environment(
            type_version_name, 1, 1, UINT32_MAX)),
    };
}

sidecar::network::DataPathMode data_path_from_environment() {
    const std::string value =
        environment_or("SIDECAR_UCX_DATA_PATH", "functional");
    if (value == "functional") {
        return sidecar::network::DataPathMode::functional;
    }
    if (value == "strict-rdma") {
        return sidecar::network::DataPathMode::strict_rdma;
    }
    throw std::invalid_argument(
        "SIDECAR_UCX_DATA_PATH must be functional or strict-rdma");
}

struct TransportConfig {
    std::string role;
    std::string bind_host;
    std::string peer_host;
    std::uint16_t port;
    std::chrono::milliseconds connect_timeout;
    sidecar::network::DataPathMode data_path;
};

TransportConfig transport_config_from_environment() {
    TransportConfig config{
        environment_or("SIDECAR_UCX_ROLE", "listen"),
        environment_or("SIDECAR_UCX_BIND_HOST", "0.0.0.0"),
        environment_or("SIDECAR_UCX_PEER_HOST", "127.0.0.1"),
        static_cast<std::uint16_t>(size_from_environment(
            "SIDECAR_UCX_PORT", 13337, 1, 65535)),
        std::chrono::milliseconds{size_from_environment(
            "SIDECAR_UCX_CONNECT_TIMEOUT_MS",
            2'000,
            1,
            std::numeric_limits<std::uint32_t>::max())},
        data_path_from_environment(),
    };

    if (config.role != "listen" && config.role != "connect") {
        throw std::invalid_argument(
            "SIDECAR_UCX_ROLE must be listen or connect");
    }
    return config;
}

sidecar::network::UCXTransport create_transport_once(
    const TransportConfig& config) {
    if (config.role == "listen") {
        return sidecar::network::UCXTransport::accept_one(
            sidecar::network::EndpointOptions{
                config.bind_host,
                config.port,
                config.connect_timeout,
                config.data_path,
            });
    }
    return sidecar::network::UCXTransport::connect(
        sidecar::network::EndpointOptions{
            config.peer_host,
            config.port,
            config.connect_timeout,
            config.data_path,
        });
}

class OwnedRing {
public:
    OwnedRing(std::string name, const RingBufferConfig& config)
        : name_(std::move(name)),
          ring_(ringbuf_create(name_.c_str(), config)) {}

    OwnedRing(const OwnedRing&) = delete;
    OwnedRing& operator=(const OwnedRing&) = delete;

    ~OwnedRing() {
        ringbuf_shutdown(ring_);
        ringbuf_close(ring_);
        ringbuf_unlink(name_.c_str());
    }

    [[nodiscard]] RingBuffer* get() const noexcept {
        return ring_;
    }

private:
    std::string name_;
    RingBuffer* ring_;
};

bool snapshot_ring(
    const RingBuffer* ring,
    sidecar::telemetry::RingSnapshot& output) noexcept {
    const std::uint64_t slot_count =
        ring->header->slot_count;
    const std::uint64_t max_payload =
        ring->header->max_payload_bytes;
    for (int attempt = 0; attempt < 3; ++attempt) {
        const std::uint64_t read =
            ring->header->read_position.load(
                std::memory_order_acquire);
        const std::uint64_t write =
            ring->header->write_position.load(
                std::memory_order_acquire);
        if (write >= read && write - read <= slot_count) {
            output = sidecar::telemetry::RingSnapshot{
                slot_count * max_payload,
                (write - read) * max_payload,
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
        const TransportConfig transport_config =
            transport_config_from_environment();
        const RingBufferConfig upstream_config =
            ring_config_from_environment(
                "SIDECAR_UPSTREAM_SLOT_COUNT",
                "SIDECAR_UPSTREAM_MAX_PAYLOAD_BYTES",
                "SIDECAR_UPSTREAM_TYPE_ID",
                "SIDECAR_UPSTREAM_TYPE_VERSION");
        const RingBufferConfig downstream_config =
            ring_config_from_environment(
                "SIDECAR_DOWNSTREAM_SLOT_COUNT",
                "SIDECAR_DOWNSTREAM_MAX_PAYLOAD_BYTES",
                "SIDECAR_DOWNSTREAM_TYPE_ID",
                "SIDECAR_DOWNSTREAM_TYPE_VERSION");
        OwnedRing upstream{
            environment_or(
                "UESTCRADAR_UPSTREAM_SHM_NAME",
                kUpstreamBufName),
            upstream_config};
        OwnedRing downstream{
            environment_or(
                "UESTCRADAR_DOWNSTREAM_SHM_NAME",
                kDownstreamBufName),
            downstream_config};

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

        std::exception_ptr forwarder_error;
        std::thread telemetry_thread([&] {
            try {
                if (sidecar::telemetry::run_telemetry_exporter(
                        running,
                        targets) != 0) {
                    throw std::runtime_error(
                        "telemetry exporter failed");
                }
            } catch (const std::exception& error) {
                std::cerr
                    << "sidecar: telemetry disabled ("
                    << error.what() << ')' << std::endl;
            }
        });
        std::thread forwarder_thread([&] {
            while (running != 0) {
                try {
                    sidecar::network::UCXTransport transport =
                        create_transport_once(transport_config);
                    sidecar::network::UCXMemoryRegion upstream_memory =
                        transport.register_memory(
                            ringbuf_storage(upstream.get()));
                    sidecar::network::UCXMemoryRegion downstream_memory =
                        transport.register_memory(
                            ringbuf_storage(downstream.get()));

                    std::cout << "sidecar: UCX peer connected" << std::endl;
                    try {
                        sidecar::forwarder::run_forwarder(
                            running,
                            upstream.get(),
                            downstream.get(),
                            transport,
                            upstream_memory,
                            downstream_memory);
                    } catch (...) {
                        forwarder_error = std::current_exception();
                        running = 0;
                    }
                    return;
                } catch (const std::exception& error) {
                    if (running == 0) {
                        return;
                    }
                    std::cerr
                        << "sidecar: UCX peer unavailable ("
                        << error.what() << "), retrying" << std::endl;
                    std::this_thread::sleep_for(
                        std::chrono::milliseconds{250});
                }
            }
        });

        std::cout
            << "sidecar: fixed-slot rings ready, upstream="
            << upstream_config.slot_count << "x"
            << upstream_config.max_payload_bytes
            << " downstream=" << downstream_config.slot_count << "x"
            << downstream_config.max_payload_bytes << std::endl;
        std::cout
            << "sidecar: telemetry started; waiting for UCX peer"
            << std::endl;

        while (running != 0) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(100));
        }

        forwarder_thread.join();
        ringbuf_shutdown(upstream.get());
        ringbuf_shutdown(downstream.get());
        telemetry_thread.join();
        if (forwarder_error != nullptr) {
            std::rethrow_exception(forwarder_error);
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "sidecar: " << error.what() << '\n';
        return 1;
    }
}
