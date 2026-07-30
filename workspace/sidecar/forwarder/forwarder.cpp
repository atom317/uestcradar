#include "forwarder.hpp"

#include "forwarder_protocol.hpp"
#include "network/ucx_transport.hpp"
#include "ringbuf/ringbuf.hpp"

#include <chrono>
#include <limits>
#include <stdexcept>
#include <thread>

namespace sidecar::forwarder {
namespace {

using network::UCXMemoryRegion;
using network::UCXRequest;
using network::UCXTransport;

protocol::PortContract contract(const RingBuffer* ring) {
    return {
        ring->header->type_id,
        ring->header->type_version,
        static_cast<std::uint32_t>(
            ring->header->max_payload_bytes),
    };
}

void exchange_and_validate_contracts(
    RingBuffer* upstream,
    RingBuffer* downstream,
    UCXTransport& transport) {
    const protocol::Hello local{
        contract(downstream),
        contract(upstream),
    };
    protocol::HelloBytes outgoing = protocol::encode_hello(local);
    protocol::HelloBytes incoming{};
    UCXRequest receive =
        transport.receive(incoming, protocol::kHelloTag);
    UCXRequest send =
        transport.send(outgoing, protocol::kHelloTag);
    transport.wait(send);
    transport.wait(receive);
    if (receive.bytes_transferred() != incoming.size()) {
        throw std::runtime_error("forwarder hello has invalid length");
    }
    protocol::Hello remote{};
    if (!protocol::decode_hello(incoming, remote)) {
        throw std::runtime_error("forwarder hello is invalid");
    }
    const bool outbound_compatible =
        local.outbound.type_id == remote.inbound.type_id &&
        local.outbound.type_version ==
            remote.inbound.type_version &&
        local.outbound.max_payload_bytes <=
            remote.inbound.max_payload_bytes;
    const bool inbound_compatible =
        local.inbound.type_id == remote.outbound.type_id &&
        local.inbound.type_version ==
            remote.outbound.type_version &&
        remote.outbound.max_payload_bytes <=
            local.inbound.max_payload_bytes;
    if (!outbound_compatible || !inbound_compatible) {
        throw std::runtime_error(
            "forwarder peer port contracts are incompatible");
    }
}

class OutboundPump {
public:
    OutboundPump(
        RingBuffer* ring,
        UCXTransport& transport,
        const UCXMemoryRegion& memory)
        : ring_(ring), transport_(transport), memory_(memory) {}

    bool progress() {
        bool activity = false;
        if (!read_lease_.active()) {
            const RingResult result =
                ringbuf_acquire(ring_, read_lease_);
            if (result == RingResult::ok) {
                activity = true;
            } else if (result != RingResult::would_block &&
                       result != RingResult::shutdown) {
                throw std::runtime_error(
                    "forwarder could not acquire source slot");
            }
        }
        if (!credit_receive_active_ && !credit_available_) {
            credit_receive_ = transport_.receive(
                credit_bytes_, protocol::kCreditTag);
            credit_receive_active_ = true;
            activity = true;
        }
        if (credit_receive_active_ && credit_receive_.completed()) {
            transport_.wait(credit_receive_);
            if (credit_receive_.bytes_transferred() !=
                credit_bytes_.size()) {
                throw std::runtime_error("invalid credit message");
            }
            const std::uint64_t decoded =
                protocol::decode_credit(credit_bytes_);
            if (decoded == 0 ||
                decoded > std::numeric_limits<std::size_t>::max()) {
                throw std::runtime_error("invalid credit value");
            }
            credit_ = static_cast<std::size_t>(decoded);
            credit_receive_active_ = false;
            credit_available_ = true;
            activity = true;
        }
        if (read_lease_.active() && credit_available_ &&
            !payload_send_active_) {
            if (read_lease_.payload().size() > credit_) {
                throw std::runtime_error(
                    "record exceeds peer receive slot");
            }
            payload_length_ = read_lease_.payload().size();
            payload_send_ = transport_.send(
                read_lease_.payload(),
                protocol::kPayloadTag,
                &memory_);
            payload_send_active_ = true;
            activity = true;
        }
        if (payload_send_active_ && payload_send_.completed()) {
            transport_.wait(payload_send_);
            if (payload_send_.bytes_transferred() != payload_length_ ||
                ringbuf_release(read_lease_) != RingResult::ok) {
                throw std::runtime_error(
                    "forwarder could not release sent slot");
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
    RingReadLease read_lease_;
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
        const UCXMemoryRegion& memory)
        : ring_(ring), transport_(transport), memory_(memory) {}

    bool progress() {
        bool activity = false;
        if (!active_) {
            const RingResult result =
                ringbuf_reserve(ring_, write_lease_);
            if (result == RingResult::ok) {
                const std::size_t capacity =
                    write_lease_.payload().size();
                payload_receive_ = transport_.receive(
                    write_lease_.payload(),
                    protocol::kPayloadTag,
                    UINT64_MAX,
                    &memory_);
                credit_bytes_ = protocol::encode_credit(capacity);
                credit_send_ = transport_.send(
                    credit_bytes_, protocol::kCreditTag);
                active_ = true;
                activity = true;
            } else if (result != RingResult::would_block &&
                       result != RingResult::shutdown) {
                throw std::runtime_error(
                    "forwarder could not reserve destination slot");
            }
        }
        if (active_ && !payload_committed_ &&
            payload_receive_.completed()) {
            try {
                transport_.wait(payload_receive_);
                const std::size_t received =
                    payload_receive_.bytes_transferred();
                if (received == 0 ||
                    received > write_lease_.payload().size() ||
                    ringbuf_commit(write_lease_, received) !=
                        RingResult::ok) {
                    throw std::runtime_error(
                        "forwarder received invalid record length");
                }
            } catch (...) {
                ringbuf_cancel(write_lease_);
                throw;
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
            activity = true;
        }
        return activity;
    }

private:
    RingBuffer* ring_;
    UCXTransport& transport_;
    const UCXMemoryRegion& memory_;
    RingWriteLease write_lease_;
    protocol::CreditBytes credit_bytes_{};
    UCXRequest credit_send_;
    UCXRequest payload_receive_;
    bool active_{false};
    bool payload_committed_{false};
    bool credit_completed_{false};
};

class IdleBackoff {
public:
    void update(bool activity) {
        if (activity) {
            idle_iterations_ = 0;
        } else if (++idle_iterations_ <= 64) {
            return;
        } else if (idle_iterations_ <= 128) {
            std::this_thread::yield();
        } else {
            std::this_thread::sleep_for(std::chrono::microseconds{50});
        }
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
    const UCXMemoryRegion& downstream_memory) {
    if (upstream == nullptr || downstream == nullptr ||
        !upstream_memory.valid() || !downstream_memory.valid()) {
        throw std::invalid_argument(
            "forwarder requires rings and registered memory");
    }
    exchange_and_validate_contracts(upstream, downstream, transport);
    OutboundPump outbound{downstream, transport, downstream_memory};
    InboundPump inbound{upstream, transport, upstream_memory};
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
