#include "sim_sink.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace uestcradar::nodes {
namespace {

std::size_t PositiveSize(const cy::flowgraph::ValueMap& params,
                         const char* key) {
    const auto value = cy::flowgraph::value_at<std::int64_t>(params, key);
    if (value <= 0) {
        throw std::invalid_argument(std::string(key) + " must be positive");
    }
    return static_cast<std::size_t>(value);
}

std::string RequiredPath(const cy::flowgraph::ValueMap& params) {
    const auto path = cy::flowgraph::value_at<std::string>(params, "file_path");
    if (path.empty() || std::filesystem::path(path).extension() != ".bin") {
        throw std::invalid_argument("file_path must name a .bin file");
    }
    return path;
}

std::uint64_t InitialSequence(const cy::flowgraph::ValueMap& params) {
    const auto value =
        cy::flowgraph::value_or<std::int64_t>(params, "initial_sequence_id", 0);
    if (value < 0) {
        throw std::invalid_argument("initial_sequence_id must not be negative");
    }
    return static_cast<std::uint64_t>(value);
}

} // namespace

SimSink::SimSink(const cy::flowgraph::ValueMap& params)
    : file_path_(RequiredPath(params)),
      configured_points_(PositiveSize(params, "points")),
      expected_sequence_id_(InitialSequence(params)) {
    if (configured_points_ > std::numeric_limits<std::uint32_t>::max()) {
        throw std::overflow_error("IQ frame points exceed uint32");
    }
    maximum_elements_ = configured_points_;
    if (maximum_elements_ >
        (std::numeric_limits<std::size_t>::max() - sizeof(IQFrameHeader)) /
            sizeof(cy::common::CS16)) {
        throw std::overflow_error("IQ frame byte count overflows size_t");
    }
    const std::size_t maximum_payload_bytes =
        sizeof(IQFrameHeader) +
        maximum_elements_ * sizeof(cy::common::CS16);
    maximum_wire_bytes_ =
        cycore::sdk::WireFrameBytes(maximum_payload_bytes);

    std::ifstream file(file_path_, std::ios::binary);
    if (!file) {
        throw std::runtime_error("failed to open reference IQ .bin file");
    }
    file.seekg(0, std::ios::end);
    const auto end = file.tellg();
    if (end <= 0) {
        throw std::invalid_argument("reference IQ .bin file must not be empty");
    }
    const auto file_bytes = static_cast<std::uint64_t>(end);
    if (file_bytes >
        static_cast<std::uint64_t>(
            std::numeric_limits<std::streamsize>::max())) {
        throw std::overflow_error("reference IQ .bin exceeds stream read range");
    }
    if (file_bytes % sizeof(cy::common::CS16) != 0) {
        throw std::invalid_argument(
            "reference IQ .bin byte count is not CS16 aligned");
    }
    const std::uint64_t elements =
        file_bytes / sizeof(cy::common::CS16);
    if (elements > std::numeric_limits<std::size_t>::max()) {
        throw std::overflow_error("reference IQ element count overflows size_t");
    }
    reference_.resize(static_cast<std::size_t>(elements));
    total_points_ = reference_.size();

    file.clear();
    file.seekg(0, std::ios::beg);
    file.read(
        reinterpret_cast<char*>(reference_.data()),
        static_cast<std::streamsize>(file_bytes));
    if (file.gcount() != static_cast<std::streamsize>(file_bytes)) {
        throw std::runtime_error("short read from reference IQ .bin file");
    }

    input_staging_.resize(maximum_wire_bytes_);
    frame_.payload.reserve(maximum_elements_);
}

bool SimSink::process_work() {
    if (in.available() < cycore::sdk::kFrameEnvelopeBytes) {
        return false;
    }

    auto envelope = cy::common::Span<std::byte>(
        input_staging_.data(), cycore::sdk::kFrameEnvelopeBytes);
    if (in.peek_copy(0, envelope) != envelope.size()) {
        return false;
    }
    auto inspection = cycore::sdk::InspectFrame(
        cy::common::Span<const std::byte>(
            envelope.data(), envelope.size()),
        maximum_wire_bytes_);
    if (inspection.status == cycore::sdk::FrameParseStatus::InvalidFrame) {
        ++stats_.framing_errors;
        consume_or_throw(inspection.discard_bytes);
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
    inspection = cycore::sdk::InspectFrame(
        cy::common::Span<const std::byte>(wire.data(), wire.size()),
        maximum_wire_bytes_);
    if (inspection.status != cycore::sdk::FrameParseStatus::CompleteFrame) {
        ++stats_.framing_errors;
        consume_or_throw(
            inspection.discard_bytes == 0
                ? inspection.wire_bytes
                : inspection.discard_bytes);
        return true;
    }

    ++stats_.frames_received;
    bool valid = true;
    if (inspection.metadata.sequence_id != expected_sequence_id_) {
        ++stats_.sequence_errors;
        valid = false;
    }
    ++expected_sequence_id_;
    if (inspection.metadata.timestamp_unix_nano == 0 ||
        inspection.metadata.timestamp_unix_nano <= last_timestamp_) {
        ++stats_.timestamp_errors;
        valid = false;
    }
    if (inspection.metadata.timestamp_unix_nano > last_timestamp_) {
        last_timestamp_ = inspection.metadata.timestamp_unix_nano;
    }

    const std::size_t expected_points =
        std::min(configured_points_,
                 total_points_ - expected_point_offset_);
    const bool decoded =
        cycore::sdk::FrameDataCodec<IQFrame>::decode(
            cy::common::Span<const std::byte>(
                wire.data() + cycore::sdk::kFrameEnvelopeBytes,
                inspection.payload_bytes),
            frame_);
    if (!decoded) {
        ++stats_.codec_errors;
        valid = false;
    } else {
        if (frame_.header.points != expected_points ||
            frame_.payload.size() != frame_.header.points) {
            ++stats_.header_errors;
            valid = false;
        }
        if (frame_.payload.size() != expected_points ||
            !std::equal(
                frame_.payload.begin(),
                frame_.payload.end(),
                reference_.begin() +
                    expected_point_offset_)) {
            ++stats_.payload_errors;
            valid = false;
        }
    }

    if (expected_points == configured_points_) {
        ++stats_.full_frames;
    } else {
        ++stats_.tail_frames;
    }
    if (valid) {
        ++stats_.frames_valid;
    }
    consume_or_throw(inspection.wire_bytes);
    advance_expected_frame();
    return true;
}

void SimSink::advance_expected_frame() noexcept {
    const std::size_t remaining = total_points_ - expected_point_offset_;
    expected_point_offset_ += std::min(configured_points_, remaining);
    if (expected_point_offset_ == total_points_) {
        expected_point_offset_ = 0;
    }
}

void SimSink::consume_or_throw(std::size_t bytes) {
    if (bytes == 0 || !in.consume_exact(bytes)) {
        throw std::runtime_error("SimSink failed to consume inspected bytes");
    }
}

} // namespace uestcradar::nodes
