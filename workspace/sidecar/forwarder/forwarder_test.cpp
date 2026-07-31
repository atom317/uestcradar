#include "forwarder/forwarder.hpp"
#include "network/ucx_transport.hpp"
#include "ringbuf/ringbuf.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <future>
#include <iostream>
#include <span>
#include <string>
#include <thread>
#include <vector>
#include <unistd.h>

namespace {

using sidecar::network::EndpointOptions;
using sidecar::network::UCXMemoryRegion;
using sidecar::network::UCXTransport;

class TestRing {
public:
    TestRing(std::string name, RingBufferConfig config)
        : name_(std::move(name)),
          ring_(ringbuf_create(name_.c_str(), config)) {}

    TestRing(const TestRing&) = delete;
    TestRing& operator=(const TestRing&) = delete;

    ~TestRing() {
        ringbuf_shutdown(ring_);
        ringbuf_close(ring_);
        ringbuf_unlink(name_.c_str());
    }

    RingBuffer* get() const noexcept {
        return ring_;
    }

private:
    std::string name_;
    RingBuffer* ring_;
};

bool write_all(
    RingBuffer* ring,
    std::span<const std::byte> bytes) {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds{10};
    while (std::chrono::steady_clock::now() < deadline) {
        const std::int32_t result = ringbuf_write(
            ring, bytes.data(), bytes.size());
        if (result == static_cast<std::int32_t>(bytes.size())) {
            return true;
        } else if (result == 0) {
            std::this_thread::yield();
        } else {
            return false;
        }
    }
    return false;
}

bool read_all(
    RingBuffer* ring,
    std::span<std::byte> bytes) {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds{10};
    while (std::chrono::steady_clock::now() < deadline) {
        const std::int32_t result = ringbuf_read(
            ring, bytes.data(), bytes.size());
        if (result == static_cast<std::int32_t>(bytes.size())) {
            return true;
        } else if (result == 0) {
            std::this_thread::yield();
        } else {
            return false;
        }
    }
    return false;
}

void shutdown_all(
    TestRing& a_upstream,
    TestRing& a_downstream,
    TestRing& b_upstream,
    TestRing& b_downstream) {
    ringbuf_shutdown(a_upstream.get());
    ringbuf_shutdown(a_downstream.get());
    ringbuf_shutdown(b_upstream.get());
    ringbuf_shutdown(b_downstream.get());
}

}  // namespace

int main() {
    ::setenv("UCX_TLS", "tcp,self", 1);

    const std::string prefix =
        "/uestcradar_forwarder_test_" + std::to_string(::getpid());
    constexpr std::uint32_t kPayloadBytes = 256 * 1024;
    const RingBufferConfig type_one{4, kPayloadBytes, 1, 1};
    const RingBufferConfig type_two{4, kPayloadBytes, 2, 1};
    TestRing a_upstream{prefix + "_a_up", type_two};
    TestRing a_downstream{prefix + "_a_down", type_one};
    TestRing b_upstream{prefix + "_b_up", type_one};
    TestRing b_downstream{prefix + "_b_down", type_two};

    const auto port = static_cast<std::uint16_t>(
        20'000 + (::getpid() % 20'000));
    volatile std::sig_atomic_t a_running = 1;
    volatile std::sig_atomic_t b_running = 1;
    std::exception_ptr a_error;
    std::exception_ptr b_error;

    std::thread a_forwarder([&] {
        try {
            UCXTransport transport = UCXTransport::accept_one(
                EndpointOptions{
                    "127.0.0.1",
                    port,
                    std::chrono::seconds{10},
                });
            UCXMemoryRegion upstream_memory =
                transport.register_memory(
                    ringbuf_storage(a_upstream.get()));
            UCXMemoryRegion downstream_memory =
                transport.register_memory(
                    ringbuf_storage(a_downstream.get()));
            sidecar::forwarder::run_forwarder(
                a_running,
                a_upstream.get(),
                a_downstream.get(),
                transport,
                upstream_memory,
                downstream_memory);
        } catch (...) {
            a_error = std::current_exception();
            a_running = 0;
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds{100});
    std::thread b_forwarder([&] {
        try {
            UCXTransport transport = UCXTransport::connect(
                EndpointOptions{
                    "127.0.0.1",
                    port,
                    std::chrono::seconds{10},
                });
            UCXMemoryRegion upstream_memory =
                transport.register_memory(
                    ringbuf_storage(b_upstream.get()));
            UCXMemoryRegion downstream_memory =
                transport.register_memory(
                    ringbuf_storage(b_downstream.get()));
            sidecar::forwarder::run_forwarder(
                b_running,
                b_upstream.get(),
                b_downstream.get(),
                transport,
                upstream_memory,
                downstream_memory);
        } catch (...) {
            b_error = std::current_exception();
            b_running = 0;
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds{200});

    std::vector<std::byte> a_to_b(256 * 1024);
    std::vector<std::byte> b_to_a(256 * 1024);
    for (std::size_t index = 0; index < a_to_b.size(); ++index) {
        a_to_b[index] =
            static_cast<std::byte>((index * 17 + 3) & 0xff);
        b_to_a[index] =
            static_cast<std::byte>((index * 29 + 7) & 0xff);
    }
    std::vector<std::byte> received_by_a(b_to_a.size());
    std::vector<std::byte> received_by_b(a_to_b.size());

    auto write_a = std::async(
        std::launch::async,
        [&] { return write_all(a_downstream.get(), a_to_b); });
    auto write_b = std::async(
        std::launch::async,
        [&] { return write_all(b_downstream.get(), b_to_a); });
    auto read_a = std::async(
        std::launch::async,
        [&] { return read_all(a_upstream.get(), received_by_a); });
    auto read_b = std::async(
        std::launch::async,
        [&] { return read_all(b_upstream.get(), received_by_b); });

    const bool transfers_ok =
        write_a.get() && write_b.get() && read_a.get() && read_b.get();
    a_running = 0;
    b_running = 0;
    shutdown_all(
        a_upstream, a_downstream, b_upstream, b_downstream);
    a_forwarder.join();
    b_forwarder.join();

    if (a_error != nullptr || b_error != nullptr) {
        try {
            if (a_error != nullptr) {
                std::rethrow_exception(a_error);
            }
            std::rethrow_exception(b_error);
        } catch (const std::exception& error) {
            std::cerr << "forwarder-test: " << error.what() << '\n';
        }
        return 1;
    }
    if (!transfers_ok ||
        received_by_a != b_to_a ||
        received_by_b != a_to_b) {
        std::cerr << "forwarder-test: byte-stream mismatch\n";
        return 1;
    }
    return 0;
}
