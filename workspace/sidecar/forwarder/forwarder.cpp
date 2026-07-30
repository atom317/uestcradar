#include "forwarder.hpp"

#include "forwarder_protocol.hpp"
#include "network/ucx_transport.hpp"
#include "ringbuf/ringbuf.hpp"

#include <algorithm>
#include <chrono>
#include <limits>
#include <span>
#include <stdexcept>
#include <thread>

namespace sidecar::forwarder {
namespace {

using network::UCXMemoryRegion;
using network::UCXRequest;
using network::UCXTransport;

class OutboundPump {
public:
    OutboundPump(
        RingBuffer* ring,
        UCXTransport& transport,
        const UCXMemoryRegion& memory,
        std::size_t maximum)
        : ring_(ring),
          transport_(transport),
          memory_(memory),
          maximum_(maximum) {}

    bool progress() {
        bool activity = false;

        if (!credit_receive_active_ && !credit_available_) {
            credit_receive_ = transport_.receive(
                credit_bytes_,
                protocol::kCreditTag);
            credit_receive_active_ = true;
            activity = true;
        }

        if (credit_receive_active_ && credit_receive_.completed()) {
            transport_.wait(credit_receive_);
            if (credit_receive_.bytes_transferred() !=
                credit_bytes_.size()) {
                throw std::runtime_error(
                    "forwarder received an invalid credit message");
            }
            const std::uint64_t decoded =
                protocol::decode_credit(credit_bytes_);
            if (decoded == 0 ||
                decoded > std::numeric_limits<std::size_t>::max()) {
                throw std::runtime_error(
                    "forwarder received an invalid credit value");
            }
            credit_ = static_cast<std::size_t>(decoded);
            credit_receive_active_ = false;
            credit_available_ = true;
            activity = true;
        }

        if (credit_available_ && !payload_send_active_) {
            std::span<const std::byte> readable =
                ringbuf_peek_read(ring_);
            const std::size_t length = std::min(
                {readable.size(), credit_, maximum_});
            if (length != 0) {
                payload_length_ = length;
                payload_send_ = transport_.send(
                    readable.first(length),
                    protocol::kPayloadTag,
                    &memory_);
                payload_send_active_ = true;
                activity = true;
            }
        }

        if (payload_send_active_ && payload_send_.completed()) {
            transport_.wait(payload_send_);
            if (payload_send_.bytes_transferred() != payload_length_ ||
                !ringbuf_commit_read(ring_, payload_length_)) {
                throw std::runtime_error(
                    "forwarder could not commit sent ring bytes");
            }
            payload_send_active_ = false;
            credit_available_ = false;
            credit_ = 0;
            payload_length_ = 0;
            activity = true;
        }
        return activity;
    }

private:
    RingBuffer* ring_;
    UCXTransport& transport_;
    const UCXMemoryRegion& memory_;
    std::size_t maximum_;

    protocol::CreditBytes credit_bytes_{};
    UCXRequest credit_receive_;
    UCXRequest payload_send_;
    std::size_t credit_{0};
    std::size_t payload_length_{0};
    bool credit_receive_active_{false};
    bool credit_available_{false};
    bool payload_send_active_{false};
};

class InboundPump {
public:
    InboundPump(
        RingBuffer* ring,
        UCXTransport& transport,
        const UCXMemoryRegion& memory,
        std::size_t maximum)
        : ring_(ring),
          transport_(transport),
          memory_(memory),
          maximum_(maximum) {}

    bool progress() {
        bool activity = false;

        if (!active_) {
            std::span<std::byte> writable =
                ringbuf_reserve_write(ring_);
            const std::size_t length =
                std::min(writable.size(), maximum_);
            if (length != 0) {
                reserved_length_ = length;
                payload_receive_ = transport_.receive(
                    writable.first(length),
                    protocol::kPayloadTag,
                    UINT64_MAX,
                    &memory_);
                credit_bytes_ = protocol::encode_credit(length);
                credit_send_ = transport_.send(
                    credit_bytes_,
                    protocol::kCreditTag);
                active_ = true;
                activity = true;
            }
        }

        if (active_ && !payload_committed_ &&
            payload_receive_.completed()) {
            const std::size_t received =
                payload_receive_.bytes_transferred();
            transport_.wait(payload_receive_);
            if (received == 0 || received > reserved_length_ ||
                !ringbuf_commit_write(ring_, received)) {
                throw std::runtime_error(
                    "forwarder could not commit received ring bytes");
            }
            payload_committed_ = true;
            activity = true;
        }

        if (active_ && !credit_completed_ &&
            credit_send_.completed()) {
            transport_.wait(credit_send_);
            credit_completed_ = true;
            activity = true;
        }

        if (active_ && payload_committed_ && credit_completed_) {
            active_ = false;
            payload_committed_ = false;
            credit_completed_ = false;
            reserved_length_ = 0;
            activity = true;
        }
        return activity;
    }

private:
    RingBuffer* ring_;
    UCXTransport& transport_;
    const UCXMemoryRegion& memory_;
    std::size_t maximum_;

    protocol::CreditBytes credit_bytes_{};
    UCXRequest credit_send_;
    UCXRequest payload_receive_;
    std::size_t reserved_length_{0};
    bool active_{false};
    bool payload_committed_{false};
    bool credit_completed_{false};
};

class IdleBackoff {
public:
    void update(bool activity) {
        if (activity) {
            idle_iterations_ = 0;
            return;
        }
        ++idle_iterations_;
        if (idle_iterations_ <= 64) {
            return;
        }
        if (idle_iterations_ <= 128) {
            std::this_thread::yield();
            return;
        }
        std::this_thread::sleep_for(std::chrono::microseconds{50});
    }

private:
    std::size_t idle_iterations_{0};
};

}  // namespace

void run_forwarder(
    volatile std::sig_atomic_t& running,
    RingBuffer* upstream,
    RingBuffer* downstream,
    UCXTransport& transport,
    const UCXMemoryRegion& upstream_memory,
    const UCXMemoryRegion& downstream_memory,
    const ForwarderOptions& options) {
    if (upstream == nullptr || downstream == nullptr) {
        throw std::invalid_argument(
            "forwarder requires both RingBuffers");
    }
    if (!upstream_memory.valid() || !downstream_memory.valid()) {
        throw std::invalid_argument(
            "forwarder requires registered RingBuffer memory");
    }
    if (options.max_transfer_bytes == 0) {
        throw std::invalid_argument(
            "forwarder max transfer must be positive");
    }

    OutboundPump outbound{
        downstream,
        transport,
        downstream_memory,
        options.max_transfer_bytes,
    };
    InboundPump inbound{
        upstream,
        transport,
        upstream_memory,
        options.max_transfer_bytes,
    };
    IdleBackoff backoff;

    while (running != 0 &&
           !ringbuf_is_shutdown(upstream) &&
           !ringbuf_is_shutdown(downstream)) {
        bool activity = transport.progress();
        activity = outbound.progress() || activity;
        activity = inbound.progress() || activity;
        backoff.update(activity);
    }
}

}  // namespace sidecar::forwarder
