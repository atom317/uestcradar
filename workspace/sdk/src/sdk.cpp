#include <sdk.h>

#include "ringbuf/ringbuf.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>

namespace uestcradar {
namespace {

enum class FrameKind : std::uint8_t {
    iq,
    pulse_compression,
    range_doppler,
};

enum class ActiveLease : std::uint8_t {
    none,
    read,
    write,
};

template <class Frame>
struct FrameCodec;

template <>
struct FrameCodec<IQFrame> {
    static constexpr FrameKind kind = FrameKind::iq;
    static constexpr std::uint64_t type_id = 1;

    static std::size_t rows(const IQMetadata& metadata) {
        return metadata.channel_count;
    }

    static std::size_t columns(const IQMetadata& metadata) {
        return metadata.samples_per_channel;
    }
};

template <>
struct FrameCodec<PulseCompressionFrame> {
    static constexpr FrameKind kind = FrameKind::pulse_compression;
    static constexpr std::uint64_t type_id = 2;

    static std::size_t rows(
        const PulseCompressionMetadata& metadata) {
        return metadata.channel_count;
    }

    static std::size_t columns(
        const PulseCompressionMetadata& metadata) {
        return metadata.range_bin_count;
    }
};

template <>
struct FrameCodec<RDFrame> {
    static constexpr FrameKind kind = FrameKind::range_doppler;
    static constexpr std::uint64_t type_id = 3;

    static std::size_t rows(const RDMetadata& metadata) {
        return metadata.range_bin_count;
    }

    static std::size_t columns(const RDMetadata& metadata) {
        return metadata.doppler_bin_count;
    }
};

template <class Frame>
std::size_t payload_bytes(const typename Frame::Metadata& metadata) {
    const std::size_t rows = FrameCodec<Frame>::rows(metadata);
    const std::size_t columns =
        FrameCodec<Frame>::columns(metadata);
    if (rows == 0 || columns == 0 ||
        rows > std::numeric_limits<std::size_t>::max() / columns) {
        throw std::runtime_error("frame dimensions are invalid");
    }
    const std::size_t elements = rows * columns;
    if (elements >
        (std::numeric_limits<std::size_t>::max() -
         sizeof(typename Frame::Metadata)) /
            sizeof(typename Frame::Sample)) {
        throw std::runtime_error("frame payload size overflows");
    }
    return sizeof(typename Frame::Metadata) +
           elements * sizeof(typename Frame::Sample);
}

template <class Frame>
Frame map_frame(
    const typename Frame::Metadata& metadata,
    std::byte* payload,
    std::size_t length) {
    const std::size_t expected = payload_bytes<Frame>(metadata);
    if (length != expected) {
        throw std::runtime_error(
            "frame dimensions do not match payload length");
    }
    using Sample = typename Frame::Sample;
    auto* values = reinterpret_cast<Sample*>(
        payload + sizeof(typename Frame::Metadata));
    return {
        metadata,
        Array2D<Sample>{
            values,
            FrameCodec<Frame>::rows(metadata),
            FrameCodec<Frame>::columns(metadata),
        },
    };
}

const char* environment_or(
    const char* name,
    const char* fallback) noexcept {
    const char* value = std::getenv(name);
    return value == nullptr || value[0] == '\0' ? fallback : value;
}

std::uint64_t type_id(FrameKind kind) {
    switch (kind) {
        case FrameKind::iq:
            return FrameCodec<IQFrame>::type_id;
        case FrameKind::pulse_compression:
            return FrameCodec<PulseCompressionFrame>::type_id;
        case FrameKind::range_doppler:
            return FrameCodec<RDFrame>::type_id;
    }
    throw std::runtime_error("unknown frame kind");
}

std::size_t minimum_payload_bytes(FrameKind kind) {
    switch (kind) {
        case FrameKind::iq:
            return sizeof(IQMetadata) + sizeof(ComplexInt16);
        case FrameKind::pulse_compression:
            return sizeof(PulseCompressionMetadata) +
                   sizeof(ComplexFloat32);
        case FrameKind::range_doppler:
            return sizeof(RDMetadata) + sizeof(float);
    }
    throw std::runtime_error("unknown frame kind");
}

}  // namespace

namespace detail {

struct PortState {
    std::atomic<std::size_t> references{1};
    RingBuffer* ring{nullptr};
    RingReadLease read_lease;
    RingWriteLease write_lease;
    FrameKind kind{FrameKind::iq};
    ActiveLease active{ActiveLease::none};
    std::size_t write_payload_bytes{0};

    ~PortState() {
        if (active == ActiveLease::read) {
            static_cast<void>(ringbuf_release(read_lease));
        } else if (active == ActiveLease::write) {
            ringbuf_cancel(write_lease);
        }
        ringbuf_close(ring);
    }
};

void retain(PortState* state) noexcept {
    if (state != nullptr) {
        state->references.fetch_add(1, std::memory_order_relaxed);
    }
}

void abandon(PortState* state) noexcept {
    if (state == nullptr) {
        return;
    }
    if (state->active == ActiveLease::read) {
        static_cast<void>(ringbuf_release(state->read_lease));
    } else if (state->active == ActiveLease::write) {
        ringbuf_cancel(state->write_lease);
    }
    state->active = ActiveLease::none;
    state->write_payload_bytes = 0;
}

void release(PortState* state) noexcept {
    if (state != nullptr &&
        state->references.fetch_sub(
            1, std::memory_order_acq_rel) == 1) {
        delete state;
    }
}

template <class Frame>
FrameHandle<Frame> FrameAccess::make(
    Frame frame,
    PortState* state) noexcept {
    return FrameHandle<Frame>{std::move(frame), state};
}

template <class Frame>
PortState* FrameAccess::state(
    FrameHandle<Frame>& frame) noexcept {
    return frame.state_;
}

template <class Frame>
void FrameAccess::finish(FrameHandle<Frame>& frame) noexcept {
    frame.finish();
}

}  // namespace detail

namespace {

detail::PortState* open_port(
    const char* environment_name,
    const char* default_name,
    FrameKind kind) {
    auto* state = new detail::PortState;
    try {
        state->ring = ringbuf_open(
            environment_or(environment_name, default_name));
        state->kind = kind;
        if (state->ring->header->type_id != type_id(kind) ||
            state->ring->header->type_version != 1) {
            throw std::runtime_error(
                "shared-memory frame type does not match SDK port");
        }
        if (ringbuf_max_payload_bytes(state->ring) <
            minimum_payload_bytes(kind)) {
            throw std::runtime_error(
                "shared-memory max payload is too small for frame type");
        }
        return state;
    } catch (...) {
        delete state;
        throw;
    }
}

void wait_for_read(detail::PortState& state) {
    for (;;) {
        const RingResult result =
            ringbuf_acquire(state.ring, state.read_lease);
        if (result == RingResult::ok) {
            return;
        }
        if (result == RingResult::shutdown) {
            throw std::runtime_error("input has been shut down");
        }
        if (result != RingResult::would_block) {
            throw std::runtime_error("input RingBuffer is corrupt");
        }
        std::this_thread::sleep_for(std::chrono::microseconds{50});
    }
}

void wait_for_write(detail::PortState& state) {
    for (;;) {
        const RingResult result =
            ringbuf_reserve(state.ring, state.write_lease);
        if (result == RingResult::ok) {
            return;
        }
        if (result == RingResult::shutdown) {
            throw std::runtime_error("output has been shut down");
        }
        if (result != RingResult::would_block) {
            throw std::runtime_error("output RingBuffer is corrupt");
        }
        std::this_thread::sleep_for(std::chrono::microseconds{50});
    }
}

template <class Frame>
FrameHandle<Frame> read_frame(detail::PortState& state) {
    if (state.active != ActiveLease::none) {
        throw std::runtime_error(
            "the previous input frame is still alive");
    }
    wait_for_read(state);
    try {
        const auto payload = state.read_lease.payload();
        if (payload.size() < sizeof(typename Frame::Metadata)) {
            throw std::runtime_error("frame metadata is truncated");
        }
        typename Frame::Metadata metadata{};
        std::memcpy(
            &metadata, payload.data(), sizeof(metadata));
        Frame frame = map_frame<Frame>(
            metadata,
            const_cast<std::byte*>(payload.data()),
            payload.size());
        state.active = ActiveLease::read;
        return detail::FrameAccess::make(
            std::move(frame), &state);
    } catch (...) {
        static_cast<void>(ringbuf_release(state.read_lease));
        throw;
    }
}

template <class Frame>
FrameHandle<Frame> create_frame(
    detail::PortState& state,
    const typename Frame::Metadata& metadata) {
    if (state.active != ActiveLease::none) {
        throw std::runtime_error(
            "the previous output frame is still alive");
    }
    const std::size_t length = payload_bytes<Frame>(metadata);
    if (length > ringbuf_max_payload_bytes(state.ring)) {
        throw std::runtime_error(
            "frame exceeds output max payload size");
    }
    wait_for_write(state);
    try {
        Frame frame = map_frame<Frame>(
            metadata,
            state.write_lease.payload().data(),
            length);
        state.active = ActiveLease::write;
        state.write_payload_bytes = length;
        return detail::FrameAccess::make(
            std::move(frame), &state);
    } catch (...) {
        ringbuf_cancel(state.write_lease);
        throw;
    }
}

template <class Frame>
void write_frame(
    detail::PortState& state,
    FrameHandle<Frame>& frame) {
    if (detail::FrameAccess::state(frame) != &state ||
        state.active != ActiveLease::write) {
        throw std::runtime_error(
            "output frame does not belong to this port");
    }
    if (payload_bytes<Frame>(frame.metadata) !=
        state.write_payload_bytes) {
        throw std::runtime_error(
            "output frame dimensions changed after create");
    }
    std::memcpy(
        state.write_lease.payload().data(),
        &frame.metadata,
        sizeof(typename Frame::Metadata));
    if (ringbuf_commit(
            state.write_lease,
            state.write_payload_bytes) != RingResult::ok) {
        throw std::runtime_error("could not commit output frame");
    }
    state.active = ActiveLease::none;
    state.write_payload_bytes = 0;
    detail::FrameAccess::finish(frame);
}

}  // namespace

template <class Frame>
Input<Frame>::Input()
    : state_(open_port(
          "UESTCRADAR_UPSTREAM_SHM_NAME",
          kUpstreamBufName,
          FrameCodec<Frame>::kind)) {}

template <class Frame>
Input<Frame>::Input(Input&& other) noexcept
    : state_(std::exchange(other.state_, nullptr)) {}

template <class Frame>
Input<Frame>& Input<Frame>::operator=(Input&& other) noexcept {
    if (this != &other) {
        detail::release(state_);
        state_ = std::exchange(other.state_, nullptr);
    }
    return *this;
}

template <class Frame>
Input<Frame>::~Input() {
    detail::release(state_);
}

template <class Frame>
FrameHandle<Frame> Input<Frame>::read() {
    if (state_ == nullptr) {
        throw std::runtime_error("input port is not open");
    }
    return read_frame<Frame>(*state_);
}

template <class Frame>
Output<Frame>::Output()
    : state_(open_port(
          "UESTCRADAR_DOWNSTREAM_SHM_NAME",
          kDownstreamBufName,
          FrameCodec<Frame>::kind)) {}

template <class Frame>
Output<Frame>::Output(Output&& other) noexcept
    : state_(std::exchange(other.state_, nullptr)) {}

template <class Frame>
Output<Frame>& Output<Frame>::operator=(Output&& other) noexcept {
    if (this != &other) {
        detail::release(state_);
        state_ = std::exchange(other.state_, nullptr);
    }
    return *this;
}

template <class Frame>
Output<Frame>::~Output() {
    detail::release(state_);
}

template <class Frame>
FrameHandle<Frame> Output<Frame>::create(
    const typename Frame::Metadata& metadata) {
    if (state_ == nullptr) {
        throw std::runtime_error("output port is not open");
    }
    return create_frame<Frame>(*state_, metadata);
}

template <class Frame>
void Output<Frame>::write(FrameHandle<Frame>& frame) {
    if (state_ == nullptr) {
        throw std::runtime_error("output port is not open");
    }
    write_frame(*state_, frame);
}

template class Input<IQFrame>;
template class Input<PulseCompressionFrame>;
template class Input<RDFrame>;
template class Output<IQFrame>;
template class Output<PulseCompressionFrame>;
template class Output<RDFrame>;

}  // namespace uestcradar
