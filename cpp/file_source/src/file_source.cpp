#include "file_source.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
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

FileSource::FileSource(const cy::flowgraph::ValueMap& params)
    : file_path_(RequiredPath(params)),
      configured_points_(PositiveSize(params, "points")),
      next_sequence_id_(InitialSequence(params)) {
    if (configured_points_ > std::numeric_limits<std::uint32_t>::max()) {
        throw std::overflow_error("IQ frame points exceed uint32");
    }
    maximum_elements_ = configured_points_;
    if (maximum_elements_ >
        (std::numeric_limits<std::size_t>::max() - sizeof(IQFrameHeader)) /
            sizeof(cy::common::CS16)) {
        throw std::overflow_error("IQ frame byte count overflows size_t");
    }
    maximum_payload_bytes_ =
        sizeof(IQFrameHeader) +
        maximum_elements_ * sizeof(cy::common::CS16);
    if (maximum_elements_ * sizeof(cy::common::CS16) >
        static_cast<std::size_t>(
            std::numeric_limits<std::streamsize>::max())) {
        throw std::overflow_error("IQ frame exceeds stream read range");
    }
    maximum_wire_bytes_ =
        cycore::sdk::WireFrameBytes(maximum_payload_bytes_);

    file_.open(file_path_, std::ios::binary);
    if (!file_) {
        throw std::runtime_error("failed to open IQ .bin file");
    }
    file_.seekg(0, std::ios::end);
    const auto end = file_.tellg();
    if (end <= 0) {
        throw std::invalid_argument("IQ .bin file must not be empty");
    }
    const auto file_bytes = static_cast<std::uint64_t>(end);
    if (file_bytes % sizeof(cy::common::CS16) != 0) {
        throw std::invalid_argument("IQ .bin byte count is not CS16 aligned");
    }
    const std::uint64_t elements =
        file_bytes / sizeof(cy::common::CS16);
    if (elements > std::numeric_limits<std::size_t>::max()) {
        throw std::overflow_error("IQ .bin point count overflows size_t");
    }
    total_points_ = static_cast<std::size_t>(elements);

    file_.clear();
    file_.seekg(0, std::ios::beg);
    if (!file_) {
        throw std::runtime_error("failed to seek IQ .bin file");
    }

    frame_.payload.reserve(maximum_elements_);
    payload_staging_.resize(maximum_payload_bytes_);
    wire_staging_.resize(maximum_wire_bytes_);
}

bool FileSource::process_work() {
    if (pending_wire_bytes_ == 0) {
        if (out.available() == 0) {
            ++stats_.backpressure_count;
            return false;
        }
        if (!build_frame()) {
            return false;
        }
    }

    const std::size_t remaining = pending_wire_bytes_ - pending_offset_;
    auto span = out.reserve(remaining);
    if (span.empty()) {
        ++stats_.backpressure_count;
        return false;
    }
    std::memcpy(
        span.data(), wire_staging_.data() + pending_offset_, span.size());
    pending_offset_ += span.size();
    stats_.bytes_emitted += span.size();
    span.commit(span.size());

    if (pending_offset_ == pending_wire_bytes_) {
        ++stats_.frames_emitted;
        if (pending_points_ == configured_points_) {
            ++stats_.full_frames;
        } else {
            ++stats_.tail_frames;
        }
        pending_wire_bytes_ = 0;
        pending_offset_ = 0;
        pending_points_ = 0;
    }
    return true;
}

bool FileSource::build_frame() {
    const std::size_t remaining_points = total_points_ - point_offset_;
    const std::size_t points =
        std::min(configured_points_, remaining_points);
    const std::size_t elements = points;
    const std::size_t data_bytes =
        elements * sizeof(cy::common::CS16);

    frame_.header.points = static_cast<std::uint32_t>(points);
    frame_.payload.resize(elements);
    file_.read(
        reinterpret_cast<char*>(frame_.payload.data()),
        static_cast<std::streamsize>(data_bytes));
    if (file_.gcount() != static_cast<std::streamsize>(data_bytes)) {
        throw std::runtime_error("short read from IQ .bin file");
    }

    const std::size_t payload_bytes =
        cycore::sdk::FrameDataCodec<IQFrame>::encoded_size(frame_);
    if (!cycore::sdk::FrameDataCodec<IQFrame>::encode(
            frame_,
            cy::common::Span<std::byte>(
                payload_staging_.data(), payload_bytes))) {
        throw std::runtime_error("failed to encode IQFrame");
    }
    if (!cycore::sdk::EncodeFrame(
            cy::common::Span<const std::byte>(
                payload_staging_.data(), payload_bytes),
            cycore::sdk::FrameMetadata{
                next_sequence_id_++, next_timestamp()},
            cy::common::Span<std::byte>(
                wire_staging_.data(), wire_staging_.size()),
            &pending_wire_bytes_)) {
        throw std::runtime_error("failed to seal IQFrame");
    }
    pending_offset_ = 0;
    pending_points_ = points;

    point_offset_ += points;
    if (point_offset_ == total_points_) {
        rewind_file();
    }
    return true;
}

std::uint64_t FileSource::next_timestamp() {
    const auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    if (now <= 0) {
        throw std::runtime_error("system clock is before Unix epoch");
    }
    std::uint64_t timestamp = static_cast<std::uint64_t>(now);
    if (timestamp <= last_timestamp_) {
        if (last_timestamp_ == std::numeric_limits<std::uint64_t>::max()) {
            throw std::overflow_error("frame timestamp overflow");
        }
        timestamp = last_timestamp_ + 1;
    }
    last_timestamp_ = timestamp;
    return timestamp;
}

void FileSource::rewind_file() {
    file_.clear();
    file_.seekg(0, std::ios::beg);
    if (!file_) {
        throw std::runtime_error("failed to rewind IQ .bin file");
    }
    point_offset_ = 0;
    ++stats_.rewinds;
}

} // namespace uestcradar::nodes
