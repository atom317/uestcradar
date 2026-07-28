#pragma once

#include "iq_frame.h"

#include <cycore_algorithm_sdk.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace uestcradar::nodes {

struct SimSinkStats {
    std::uint64_t frames_received = 0;
    std::uint64_t frames_valid = 0;
    std::uint64_t full_frames = 0;
    std::uint64_t tail_frames = 0;
    std::uint64_t framing_errors = 0;
    std::uint64_t codec_errors = 0;
    std::uint64_t sequence_errors = 0;
    std::uint64_t timestamp_errors = 0;
    std::uint64_t header_errors = 0;
    std::uint64_t payload_errors = 0;
};

class SimSink : public cy::flowgraph::Block<SimSink> {
public:
    cy::flowgraph::PortIn<std::byte> in;
    CY_MAKE_REFLECTABLE(SimSink, in);

    explicit SimSink(const cy::flowgraph::ValueMap& params);

    bool process_work();

    const SimSinkStats& stats() const noexcept { return stats_; }
    std::size_t maximum_wire_bytes() const noexcept {
        return maximum_wire_bytes_;
    }

private:
    void advance_expected_frame() noexcept;
    void consume_or_throw(std::size_t bytes);

    std::string file_path_;
    std::size_t configured_points_ = 0;
    std::size_t total_points_ = 0;
    std::size_t expected_point_offset_ = 0;
    std::size_t maximum_elements_ = 0;
    std::size_t maximum_wire_bytes_ = 0;
    std::vector<cy::common::CS16> reference_;
    std::vector<std::byte> input_staging_;
    IQFrame frame_;
    std::uint64_t expected_sequence_id_ = 0;
    std::uint64_t last_timestamp_ = 0;
    SimSinkStats stats_{};
};

} // namespace uestcradar::nodes
