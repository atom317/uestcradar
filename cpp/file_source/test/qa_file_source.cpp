#include "file_source.h"

#include <cycore_benchmark_harness.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fg = cy::flowgraph;
namespace sdk = cycore::sdk;
using uestcradar::nodes::FileSource;
using uestcradar::nodes::IQFrame;

namespace {

void Require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

class Fixture {
public:
    Fixture(std::size_t pulses = 10, std::size_t samples = 3) {
        const auto id = std::chrono::steady_clock::now()
                            .time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
                ("file_source_" + std::to_string(id) + ".bin");
        values_.resize(pulses * samples);
        for (std::size_t i = 0; i < values_.size(); ++i) {
            values_[i] = cy::common::CS16{
                static_cast<std::int16_t>(i + 1),
                static_cast<std::int16_t>(-static_cast<std::int32_t>(i + 1))};
        }
        std::ofstream output(path_, std::ios::binary);
        output.write(
            reinterpret_cast<const char*>(values_.data()),
            static_cast<std::streamsize>(
                values_.size() * sizeof(values_.front())));
        Require(output.good(), "failed to create FileSource fixture");
    }

    ~Fixture() {
        std::error_code error;
        std::filesystem::remove(path_, error);
    }

    std::string path() const { return path_.string(); }
    const std::vector<cy::common::CS16>& values() const { return values_; }

private:
    std::filesystem::path path_;
    std::vector<cy::common::CS16> values_;
};

fg::ValueMap Params(const Fixture& fixture) {
    return {
        {"file_path", fixture.path()},
        {"pulses", std::int64_t{4}},
        {"samples_per_pulse", std::int64_t{3}},
        {"initial_sequence_id", std::int64_t{7}},
    };
}

class FrameReader {
public:
    explicit FrameReader(std::size_t maximum_wire_bytes)
        : wire_(maximum_wire_bytes) {
        frame_.payload.reserve(12);
    }

    bool read(fg::PortIn<std::byte>& input,
              std::size_t maximum_wire_bytes,
              sdk::FrameMetadata* metadata) {
        if (input.available() < sdk::kFrameEnvelopeBytes) {
            return false;
        }
        auto envelope = cy::common::Span<std::byte>(
            wire_.data(), sdk::kFrameEnvelopeBytes);
        Require(input.peek_copy(0, envelope) == envelope.size(),
                "FileSource envelope peek failed");
        auto inspection = sdk::InspectFrame(
            cy::common::Span<const std::byte>(
                envelope.data(), envelope.size()),
            maximum_wire_bytes);
        if (inspection.wire_bytes == 0 ||
            input.available() < inspection.wire_bytes) {
            return false;
        }
        auto wire = cy::common::Span<std::byte>(
            wire_.data(), inspection.wire_bytes);
        Require(input.peek_copy(0, wire) == wire.size(),
                "FileSource wire peek failed");
        inspection = sdk::InspectFrame(
            cy::common::Span<const std::byte>(wire.data(), wire.size()),
            maximum_wire_bytes);
        Require(inspection.status == sdk::FrameParseStatus::CompleteFrame,
                "FileSource emitted invalid frame");
        Require(sdk::FrameDataCodec<IQFrame>::decode(
                    cy::common::Span<const std::byte>(
                        wire.data() + sdk::kFrameEnvelopeBytes,
                        inspection.payload_bytes),
                    frame_),
                "FileSource IQFrame decode failed");
        Require(input.consume_exact(inspection.wire_bytes),
                "FileSource test failed to consume frame");
        *metadata = inspection.metadata;
        return true;
    }

    const IQFrame& frame() const noexcept { return frame_; }

private:
    std::vector<std::byte> wire_;
    IQFrame frame_;
};

void DriveOne(FileSource& source,
              fg::PortIn<std::byte>& input,
              FrameReader& reader,
              sdk::FrameMetadata* metadata) {
    for (std::size_t attempts = 0; attempts < 64; ++attempts) {
        source.process_work();
        if (reader.read(input, source.maximum_wire_bytes(), metadata)) {
            return;
        }
    }
    throw std::runtime_error("FileSource did not emit one complete frame");
}

void TestLoopingVariableFrames() {
    Fixture fixture;
    FileSource source(Params(fixture));
    fg::PortIn<std::byte> sink;
    fg::connect(source.out, sink, source.maximum_wire_bytes());
    FrameReader reader(source.maximum_wire_bytes());

    const std::array<std::uint32_t, 7> pulses{4, 4, 2, 4, 4, 2, 4};
    std::size_t reference_offset = 0;
    std::uint64_t previous_timestamp = 0;
    for (std::size_t index = 0; index < pulses.size(); ++index) {
        sdk::FrameMetadata metadata;
        DriveOne(source, sink, reader, &metadata);
        const auto& frame = reader.frame();
        Require(frame.header.pulses == pulses[index],
                "FileSource pulses sequence mismatch");
        Require(frame.header.samples_per_pulse == 3,
                "FileSource samples_per_pulse mismatch");
        Require(frame.payload.size() == pulses[index] * 3,
                "FileSource payload size mismatch");
        Require(metadata.sequence_id == 7 + index,
                "FileSource sequence mismatch");
        Require(metadata.timestamp_unix_nano > previous_timestamp,
                "FileSource timestamp is not strictly increasing");
        previous_timestamp = metadata.timestamp_unix_nano;
        for (std::size_t i = 0; i < frame.payload.size(); ++i) {
            Require(
                frame.payload[i] ==
                    fixture.values()[(reference_offset + i) %
                                     fixture.values().size()],
                "FileSource payload differs from .bin");
        }
        reference_offset =
            (reference_offset + frame.payload.size()) % fixture.values().size();
    }
    Require(source.stats().full_frames == 5,
            "FileSource full-frame count mismatch");
    Require(source.stats().tail_frames == 2,
            "FileSource tail-frame count mismatch");
    Require(source.stats().rewinds == 2,
            "FileSource rewind count mismatch");
}

void TestBackpressureAndZeroAllocations() {
    Fixture fixture;
    FileSource source(Params(fixture));
    fg::PortIn<std::byte> sink;
    fg::connect(source.out, sink, source.maximum_wire_bytes());
    FrameReader reader(source.maximum_wire_bytes());

    sdk::FrameMetadata metadata;
    for (std::size_t i = 0; i < 3; ++i) {
        DriveOne(source, sink, reader, &metadata);
    }

    cycore::benchmark::detail::begin_allocation_tracking();
    for (std::size_t i = 0; i < 30; ++i) {
        DriveOne(source, sink, reader, &metadata);
    }
    const std::size_t allocations =
        cycore::benchmark::detail::end_allocation_tracking();
    Require(allocations == 0,
            "FileSource allocated memory during steady-state work");

    while (source.process_work()) {
    }
    Require(source.stats().backpressure_count != 0,
            "FileSource did not report output backpressure");
}

void TestInvalidFiles() {
    Fixture fixture;
    auto params = Params(fixture);
    params["file_path"] = fixture.path() + ".txt";
    bool threw = false;
    try {
        FileSource source(params);
        (void)source;
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    Require(threw, "FileSource accepted a non-.bin path");

    params = Params(fixture);
    params["pulses"] = std::int64_t{0};
    threw = false;
    try {
        FileSource source(params);
        (void)source;
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    Require(threw, "FileSource accepted zero pulses");

    const std::array<std::pair<std::string, std::vector<std::byte>>, 3>
        invalid_files{{
            {fixture.path() + ".empty.bin", {}},
            {fixture.path() + ".unaligned.bin",
             {std::byte{1}, std::byte{2}, std::byte{3}}},
            {fixture.path() + ".partial-pulse.bin",
             std::vector<std::byte>(sizeof(cy::common::CS16))},
        }};
    for (const auto& invalid : invalid_files) {
        {
            std::ofstream output(invalid.first, std::ios::binary);
            if (!invalid.second.empty()) {
                output.write(
                    reinterpret_cast<const char*>(invalid.second.data()),
                    static_cast<std::streamsize>(invalid.second.size()));
            }
        }
        params = Params(fixture);
        params["file_path"] = invalid.first;
        threw = false;
        try {
            FileSource source(params);
            (void)source;
        } catch (const std::exception&) {
            threw = true;
        }
        std::error_code error;
        std::filesystem::remove(invalid.first, error);
        Require(threw, "FileSource accepted an invalid .bin file");
    }

    params = Params(fixture);
    params["file_path"] = fixture.path() + ".missing.bin";
    threw = false;
    try {
        FileSource source(params);
        (void)source;
    } catch (const std::runtime_error&) {
        threw = true;
    }
    Require(threw, "FileSource accepted a missing .bin file");
}

} // namespace

CYCORE_BENCHMARK_DEFINE_ALLOCATION_OPERATORS()

int main() {
    TestLoopingVariableFrames();
    TestBackpressureAndZeroAllocations();
    TestInvalidFiles();
    return 0;
}
