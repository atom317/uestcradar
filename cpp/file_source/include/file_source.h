#pragma once

#include "iq_frame.h"

#include <cycore_algorithm_sdk.h>

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace uestcradar::nodes {

struct FileSourceStats {
    std::uint64_t frames_emitted = 0;
    std::uint64_t bytes_emitted = 0;
    std::uint64_t full_frames = 0;
    std::uint64_t tail_frames = 0;
    std::uint64_t rewinds = 0;
    std::uint64_t backpressure_count = 0;
};

class FileSource : public cy::flowgraph::Block<FileSource> {
public:
    cy::flowgraph::PortOut<std::byte> out;
    CY_MAKE_REFLECTABLE(FileSource, out);

    explicit FileSource(const cy::flowgraph::ValueMap& params);

    bool process_work();

    const FileSourceStats& stats() const noexcept { return stats_; }
    std::size_t maximum_wire_bytes() const noexcept {
        return maximum_wire_bytes_;
    }

private:
    bool build_frame();
    std::uint64_t next_timestamp();
    void rewind_file();

    std::string file_path_;
    std::ifstream file_;
    std::size_t configured_points_ = 0;
    std::size_t total_points_ = 0;
    std::size_t point_offset_ = 0;
    std::size_t maximum_elements_ = 0;
    std::size_t maximum_payload_bytes_ = 0;
    std::size_t maximum_wire_bytes_ = 0;
    IQFrame frame_;
    std::vector<std::byte> payload_staging_;
    std::vector<std::byte> wire_staging_;
    std::size_t pending_wire_bytes_ = 0;
    std::size_t pending_offset_ = 0;
    std::size_t pending_points_ = 0;
    std::uint64_t next_sequence_id_ = 0;
    std::uint64_t last_timestamp_ = 0;
    FileSourceStats stats_{};
};

} // namespace uestcradar::nodes
