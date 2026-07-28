#pragma once

#include <flowgraph/block.h>
#include <flowgraph/plugin.h>
#include <flowgraph/port.h>
#include <flowgraph/value.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace cycore::sdk {

class Params {
public:
    explicit Params(const cy::flowgraph::ValueMap& values) : values_(values) {}

    template <typename T>
    T get(const std::string& key, const T& default_val) const {
        return cy::flowgraph::value_or<T>(values_, key, default_val);
    }

private:
    const cy::flowgraph::ValueMap& values_;
};

// ─── Frame-level byte transport ──────────────────────────────────────────────

inline constexpr std::uint32_t kFrameMagic = 0x52465943U; // "CYFR" on wire
inline constexpr std::uint16_t kFrameVersion = 1;
inline constexpr std::size_t kFrameEnvelopeBytes = 32;
inline constexpr std::size_t kMinimumWireFrameBytes = kFrameEnvelopeBytes + 1;

struct FrameMetadata {
    std::uint64_t sequence_id = 0;
    std::uint64_t timestamp_unix_nano = 0;
};

enum class ProcessResult {
    Produced,
    Retry,
    Drop,
};

enum class FrameParseStatus {
    NeedMoreData,
    InvalidFrame,
    CompleteFrame,
};

enum class FrameError {
    None,
    BadMagic,
    UnsupportedVersion,
    BadEnvelopeSize,
    BadFrameSize,
    FrameTooLarge,
};

struct FrameInspection {
    FrameParseStatus status = FrameParseStatus::NeedMoreData;
    FrameError error = FrameError::None;
    FrameMetadata metadata{};
    std::size_t wire_bytes = 0;
    std::size_t payload_bytes = 0;
    std::size_t discard_bytes = 0;
};

struct FrameStats {
    std::uint64_t frames_received = 0;
    std::uint64_t frames_processed = 0;
    std::uint64_t frames_emitted = 0;
    std::uint64_t frames_dropped = 0;
    std::uint64_t invalid_magic = 0;
    std::uint64_t invalid_version = 0;
    std::uint64_t invalid_length = 0;
    std::uint64_t codec_failures = 0;
    std::uint64_t sequence_gaps = 0;
    std::uint64_t duplicate_sequences = 0;
    std::uint64_t out_of_order_sequences = 0;
    std::uint64_t work_retries = 0;
    std::uint64_t output_backpressure_count = 0;
};

template <typename T>
struct TrivialFrameCodec {
    static_assert(
        std::is_trivially_copyable_v<T>,
        "Non-trivial data requires a custom FrameCodec, POD only");

    static constexpr std::size_t encoded_size(const T&) noexcept {
        return sizeof(T);
    }

    static bool encode(const T& value,
                       cy::common::Span<std::byte> payload) noexcept {
        if (payload.size() != sizeof(T)) {
            return false;
        }
        std::memcpy(payload.data(), std::addressof(value), sizeof(T));
        return true;
    }

    static bool decode(cy::common::Span<const std::byte> payload,
                       T& value) noexcept {
        if (payload.size() != sizeof(T)) {
            return false;
        }
        std::memcpy(std::addressof(value), payload.data(), sizeof(T));
        return true;
    }
};

namespace detail {

inline std::uint16_t read_u16_le(const std::byte* data) noexcept {
    return static_cast<std::uint16_t>(
        std::to_integer<std::uint8_t>(data[0]) |
        (static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(data[1])) << 8U));
}

inline std::uint32_t read_u32_le(const std::byte* data) noexcept {
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(data[i]))
                 << (i * 8U);
    }
    return value;
}

inline std::uint64_t read_u64_le(const std::byte* data) noexcept {
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < 8; ++i) {
        value |= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(data[i]))
                 << (i * 8U);
    }
    return value;
}

inline void write_u16_le(std::byte* data, std::uint16_t value) noexcept {
    for (std::size_t i = 0; i < 2; ++i) {
        data[i] = static_cast<std::byte>((value >> (i * 8U)) & 0xffU);
    }
}

inline void write_u32_le(std::byte* data, std::uint32_t value) noexcept {
    for (std::size_t i = 0; i < 4; ++i) {
        data[i] = static_cast<std::byte>((value >> (i * 8U)) & 0xffU);
    }
}

inline void write_u64_le(std::byte* data, std::uint64_t value) noexcept {
    for (std::size_t i = 0; i < 8; ++i) {
        data[i] = static_cast<std::byte>((value >> (i * 8U)) & 0xffU);
    }
}

inline bool checked_wire_size(std::size_t payload_bytes,
                              std::size_t* wire_bytes) noexcept {
    constexpr std::size_t overhead = kFrameEnvelopeBytes;
    if (payload_bytes == 0 ||
        payload_bytes > std::numeric_limits<std::size_t>::max() - overhead) {
        return false;
    }
    *wire_bytes = overhead + payload_bytes;
    return true;
}

inline void write_envelope(std::byte* destination,
                           std::size_t wire_bytes,
                           FrameMetadata metadata) noexcept {
    std::memset(destination, 0, kFrameEnvelopeBytes);
    write_u32_le(destination + 0, kFrameMagic);
    write_u16_le(destination + 4, kFrameVersion);
    write_u16_le(destination + 6, static_cast<std::uint16_t>(kFrameEnvelopeBytes));
    write_u64_le(destination + 8, static_cast<std::uint64_t>(wire_bytes));
    write_u64_le(destination + 16, metadata.sequence_id);
    write_u64_le(destination + 24, metadata.timestamp_unix_nano);
}

inline bool seal_frame(cy::common::Span<std::byte> destination,
                       std::size_t payload_bytes,
                       FrameMetadata metadata,
                       std::size_t* wire_bytes) noexcept {
    std::size_t total = 0;
    if (!checked_wire_size(payload_bytes, &total) || destination.size() < total) {
        return false;
    }
    write_envelope(destination.data(), total, metadata);
    if (wire_bytes) {
        *wire_bytes = total;
    }
    return true;
}

inline FrameInspection inspect_frame(cy::common::Span<const std::byte> bytes,
                                     std::size_t maximum_wire_bytes) noexcept {
    FrameInspection result;
    if (bytes.size() < kFrameEnvelopeBytes) {
        return result;
    }

    const auto* envelope = bytes.data();
    if (read_u32_le(envelope + 0) != kFrameMagic) {
        result.status = FrameParseStatus::InvalidFrame;
        result.error = FrameError::BadMagic;
        result.discard_bytes = 1;
        return result;
    }
    if (read_u16_le(envelope + 4) != kFrameVersion) {
        result.status = FrameParseStatus::InvalidFrame;
        result.error = FrameError::UnsupportedVersion;
        result.discard_bytes = 1;
        return result;
    }
    if (read_u16_le(envelope + 6) != kFrameEnvelopeBytes) {
        result.status = FrameParseStatus::InvalidFrame;
        result.error = FrameError::BadEnvelopeSize;
        result.discard_bytes = 1;
        return result;
    }
    const std::uint64_t wire_u64 = read_u64_le(envelope + 8);
    if (wire_u64 > std::numeric_limits<std::size_t>::max()) {
        result.status = FrameParseStatus::InvalidFrame;
        result.error = FrameError::BadFrameSize;
        result.discard_bytes = 1;
        return result;
    }
    result.wire_bytes = static_cast<std::size_t>(wire_u64);
    if (result.wire_bytes < kMinimumWireFrameBytes) {
        result.status = FrameParseStatus::InvalidFrame;
        result.error = FrameError::BadFrameSize;
        result.discard_bytes = 1;
        return result;
    }
    if (result.wire_bytes > maximum_wire_bytes) {
        result.status = FrameParseStatus::InvalidFrame;
        result.error = FrameError::FrameTooLarge;
        result.discard_bytes = 1;
        return result;
    }

    result.payload_bytes = result.wire_bytes - kFrameEnvelopeBytes;
    result.metadata.sequence_id = read_u64_le(envelope + 16);
    result.metadata.timestamp_unix_nano = read_u64_le(envelope + 24);
    if (bytes.size() < result.wire_bytes) {
        return result;
    }

    result.status = FrameParseStatus::CompleteFrame;
    return result;
}

} // namespace detail

inline std::size_t WireFrameBytes(std::size_t payload_bytes) {
    std::size_t wire_bytes = 0;
    if (!detail::checked_wire_size(payload_bytes, &wire_bytes)) {
        throw std::overflow_error("SDK wire frame size overflow or empty payload");
    }
    return wire_bytes;
}

inline bool EncodeFrame(cy::common::Span<const std::byte> payload,
                        FrameMetadata metadata,
                        cy::common::Span<std::byte> destination,
                        std::size_t* wire_bytes = nullptr) noexcept {
    std::size_t required = 0;
    if (!detail::checked_wire_size(payload.size(), &required) ||
        destination.size() < required) {
        return false;
    }
    std::memcpy(destination.data() + kFrameEnvelopeBytes,
                payload.data(),
                payload.size());
    return detail::seal_frame(destination, payload.size(), metadata, wire_bytes);
}

inline FrameInspection InspectFrame(cy::common::Span<const std::byte> bytes,
                                    std::size_t maximum_wire_bytes) noexcept {
    return detail::inspect_frame(bytes, maximum_wire_bytes);
}

template <typename Algorithm>
class FrameAlgorithmAdapter
    : public cy::flowgraph::Block<FrameAlgorithmAdapter<Algorithm>> {
public:
    using InputData = typename Algorithm::InputData;
    using OutputData = typename Algorithm::OutputData;
    using InputCodec = TrivialFrameCodec<InputData>;
    using OutputCodec = TrivialFrameCodec<OutputData>;

    static_assert(
        std::is_trivially_copyable_v<InputData>,
        "Non-trivial data requires a custom FrameCodec, POD only");
    static_assert(
        std::is_trivially_copyable_v<OutputData>,
        "Non-trivial data requires a custom FrameCodec, POD only");
    static_assert(std::is_default_constructible_v<InputData>,
                  "Frame algorithm InputData must be default constructible");
    static_assert(std::is_default_constructible_v<OutputData>,
                  "Frame algorithm OutputData must be default constructible");

    static constexpr std::size_t kInputWireBytes =
        kFrameEnvelopeBytes + sizeof(InputData);
    static constexpr std::size_t kOutputWireBytes =
        kFrameEnvelopeBytes + sizeof(OutputData);

    cy::flowgraph::PortIn<std::byte> in;
    cy::flowgraph::PortOut<std::byte> out;
    CY_MAKE_REFLECTABLE(FrameAlgorithmAdapter, in, out);

    explicit FrameAlgorithmAdapter(const cy::flowgraph::ValueMap& values)
        : input_staging_(kInputWireBytes),
          output_staging_(kOutputWireBytes),
          algorithm_(std::make_unique<Algorithm>(Params(values))),
          input_data_(std::make_unique<InputData>()),
          output_data_(std::make_unique<OutputData>()) {}

    bool process_work() {
        if (output_pending_) {
            return drain_output();
        }
        if (input_ready_) {
            return invoke_algorithm();
        }
        return receive_input();
    }

    const FrameStats& stats() const noexcept { return stats_; }
    bool has_pending_input() const noexcept { return input_ready_; }
    bool has_pending_output() const noexcept { return output_pending_; }

    // Pause/stop preserves decoded input and partially-published output.  A
    // resumed Adapter continues from the same state and never duplicates work.
    void on_stop() noexcept {}
    void on_pause() noexcept {}
    void on_resume() noexcept {}

private:
    bool receive_input() {
        if (in.available() < kFrameEnvelopeBytes) {
            return false;
        }

        auto header = cy::common::Span<std::byte>(
            input_staging_.data(), kFrameEnvelopeBytes);
        if (in.peek_copy(0, header) != header.size()) {
            return false;
        }
        auto inspection = InspectFrame(
            cy::common::Span<const std::byte>(header.data(), header.size()),
            kInputWireBytes);
        if (inspection.status == FrameParseStatus::InvalidFrame) {
            record_frame_error(inspection.error);
            consume_or_throw(inspection.discard_bytes);
            ++stats_.frames_dropped;
            return true;
        }
        if (inspection.wire_bytes != 0 &&
            inspection.wire_bytes != kInputWireBytes) {
            record_frame_error(FrameError::BadFrameSize);
            consume_or_throw(1);
            ++stats_.frames_dropped;
            return true;
        }
        if (inspection.wire_bytes == 0 ||
            in.available() < inspection.wire_bytes) {
            return false;
        }

        auto wire = cy::common::Span<std::byte>(
            input_staging_.data(), inspection.wire_bytes);
        if (in.peek_copy(0, wire) != wire.size()) {
            return false;
        }
        inspection = InspectFrame(
            cy::common::Span<const std::byte>(wire.data(), wire.size()),
            kInputWireBytes);
        ++stats_.frames_received;
        if (inspection.status != FrameParseStatus::CompleteFrame) {
            record_frame_error(inspection.error);
            consume_or_throw(inspection.discard_bytes == 0
                                 ? inspection.wire_bytes
                                 : inspection.discard_bytes);
            ++stats_.frames_dropped;
            return true;
        }

        const auto payload = cy::common::Span<const std::byte>(
            input_staging_.data() + kFrameEnvelopeBytes,
            inspection.payload_bytes);
        const bool decoded = InputCodec::decode(payload, *input_data_);
        consume_or_throw(inspection.wire_bytes);
        if (!decoded) {
            ++stats_.codec_failures;
            ++stats_.frames_dropped;
            return true;
        }

        update_sequence(inspection.metadata.sequence_id);
        current_metadata_ = inspection.metadata;
        input_ready_ = true;
        return invoke_algorithm();
    }

    bool invoke_algorithm() {
        const ProcessResult result =
            algorithm_->work(*input_data_, *output_data_);
        if (result == ProcessResult::Retry) {
            ++stats_.work_retries;
            return false;
        }
        if (result == ProcessResult::Drop) {
            input_ready_ = false;
            ++stats_.frames_dropped;
            return true;
        }

        constexpr std::size_t payload_bytes = sizeof(OutputData);
        std::size_t wire_bytes = 0;
        auto payload = cy::common::Span<std::byte>(
            output_staging_.data() + kFrameEnvelopeBytes, payload_bytes);
        if (!OutputCodec::encode(*output_data_, payload)) {
            ++stats_.codec_failures;
            throw std::runtime_error("Trivial frame output copy failed");
        }
        if (!detail::seal_frame(
                cy::common::Span<std::byte>(output_staging_.data(),
                                            output_staging_.size()),
                payload_bytes,
                current_metadata_,
                &wire_bytes)) {
            throw std::runtime_error("Failed to seal SDK output frame");
        }
        if (wire_bytes != kOutputWireBytes) {
            throw std::runtime_error("Trivial frame output size mismatch");
        }

        input_ready_ = false;
        output_pending_ = true;
        output_wire_bytes_ = wire_bytes;
        output_offset_ = 0;
        ++stats_.frames_processed;
        return drain_output();
    }

    bool drain_output() {
        if (!output_pending_) {
            return false;
        }
        const std::size_t remaining = output_wire_bytes_ - output_offset_;
        auto span = out.reserve(remaining);
        if (span.empty()) {
            ++stats_.output_backpressure_count;
            return false;
        }
        std::memcpy(span.data(),
                    output_staging_.data() + output_offset_,
                    span.size());
        output_offset_ += span.size();
        span.commit(span.size());
        if (output_offset_ == output_wire_bytes_) {
            output_pending_ = false;
            output_wire_bytes_ = 0;
            output_offset_ = 0;
            ++stats_.frames_emitted;
        }
        return true;
    }

    void consume_or_throw(std::size_t bytes) {
        if (bytes == 0 || !in.consume_exact(bytes)) {
            throw std::runtime_error("SDK frame parser failed to consume inspected bytes");
        }
    }

    void record_frame_error(FrameError error) noexcept {
        switch (error) {
        case FrameError::BadMagic:
            ++stats_.invalid_magic;
            break;
        case FrameError::UnsupportedVersion:
            ++stats_.invalid_version;
            break;
        case FrameError::BadEnvelopeSize:
        case FrameError::BadFrameSize:
        case FrameError::FrameTooLarge:
            ++stats_.invalid_length;
            break;
        case FrameError::None:
            break;
        }
    }

    void update_sequence(std::uint64_t sequence) noexcept {
        if (!have_sequence_) {
            have_sequence_ = true;
            last_sequence_ = sequence;
            return;
        }
        const std::uint64_t expected = last_sequence_ + 1U;
        if (sequence == last_sequence_) {
            ++stats_.duplicate_sequences;
        } else if (sequence == expected) {
            last_sequence_ = sequence;
        } else if (sequence < last_sequence_ && expected != 0) {
            ++stats_.out_of_order_sequences;
        } else {
            ++stats_.sequence_gaps;
            last_sequence_ = sequence;
        }
    }

    std::vector<std::byte> input_staging_;
    std::vector<std::byte> output_staging_;
    std::unique_ptr<Algorithm> algorithm_;
    std::unique_ptr<InputData> input_data_;
    std::unique_ptr<OutputData> output_data_;
    FrameMetadata current_metadata_{};
    FrameStats stats_{};
    std::size_t output_wire_bytes_ = 0;
    std::size_t output_offset_ = 0;
    std::uint64_t last_sequence_ = 0;
    bool have_sequence_ = false;
    bool input_ready_ = false;
    bool output_pending_ = false;
};

} // namespace cycore::sdk

namespace cycore::sdk::detail {

template <typename TAlg>
struct FrameAlgorithmRegistrar {
    static void Register(cy::flowgraph::BlockRegistry& registry,
                         const std::string& key) {
        registry.register_block<::cycore::sdk::FrameAlgorithmAdapter<TAlg>>(key);
    }
};

} // namespace cycore::sdk::detail

#define CYCORE_EXPORT_FRAME_ALGORITHM(plugin_name, block_type_name, alg_class) \
    CY_PLUGIN( \
        plugin_name, "1.0.0", "Cycore Frame SDK Plugin", "cycore", \
        ::cycore::sdk::detail::FrameAlgorithmRegistrar<alg_class>::Register( \
            plugin.block_registry(), block_type_name); \
    )
