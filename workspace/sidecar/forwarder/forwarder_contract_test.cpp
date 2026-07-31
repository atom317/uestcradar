#include "forwarder/forwarder.hpp"
#include "network/ucx_transport.hpp"
#include "ringbuf/ringbuf.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <future>
#include <string>
#include <thread>
#include <unistd.h>

namespace {

class TestRing {
public:
    TestRing(std::string name, const RingBufferConfig& config)
        : name_(std::move(name)),
          ring_(ringbuf_create(name_.c_str(), config)) {}

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

bool run_peer(
    bool listener,
    std::uint16_t port,
    RingBuffer* upstream,
    RingBuffer* downstream) {
    try {
        const sidecar::network::EndpointOptions options{
            "127.0.0.1",
            port,
            std::chrono::seconds{10},
        };
        sidecar::network::UCXTransport transport =
            listener
                ? sidecar::network::UCXTransport::accept_one(options)
                : sidecar::network::UCXTransport::connect(options);
        auto upstream_memory =
            transport.register_memory(ringbuf_storage(upstream));
        auto downstream_memory =
            transport.register_memory(ringbuf_storage(downstream));
        volatile std::sig_atomic_t running = 1;
        sidecar::forwarder::run_forwarder(
            running,
            upstream,
            downstream,
            transport,
            upstream_memory,
            downstream_memory);
    } catch (const std::exception&) {
        return true;
    }
    return false;
}

}  // namespace

int main() {
    ::setenv("UCX_TLS", "tcp,self", 1);
    const std::string prefix =
        "/uestcradar_contract_test_" + std::to_string(::getpid());
    constexpr std::uint32_t capacity = 4096;
    TestRing a_up{prefix + "_a_up", {2, capacity, 22, 1}};
    TestRing a_down{prefix + "_a_down", {2, capacity, 11, 1}};
    TestRing b_up{prefix + "_b_up", {2, capacity, 99, 1}};
    TestRing b_down{prefix + "_b_down", {2, capacity, 22, 1}};
    const auto port = static_cast<std::uint16_t>(
        40'000 + (::getpid() % 10'000));

    auto listener = std::async(
        std::launch::async,
        [&] {
            return run_peer(
                true, port, a_up.get(), a_down.get());
        });
    std::this_thread::sleep_for(std::chrono::milliseconds{100});
    auto connector = std::async(
        std::launch::async,
        [&] {
            return run_peer(
                false, port, b_up.get(), b_down.get());
        });

    return listener.get() && connector.get() ? 0 : 1;
}
