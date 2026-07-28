#include "sim_sink.h"

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
#include <vector>

namespace fg = cy::flowgraph;
namespace sdk = cycore::sdk;
using uestcradar::nodes::IQFrame;
using uestcradar::nodes::SimSink;

namespace {

void Require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

class Fixture {
public:
    Fixture() {
        const auto id = std::chrono::steady_clock::now()
                            .time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
                ("sim_sink_" + std::to_string(id) + ".bin");
        values_.resize(30);
        for (std::size_t i = 0; i < values_.size(); ++i) {
            values_[i] = cy::common::CS16{
                static_cast<std::int16_t>(i + 1),
                static_cast<std::int16_t>(1000 + i)};
        }
        std::ofstream output(path_, std::ios::binary);
        output.write(
            reinterpret_cast<const char*>(values_.data()),
            static_cast<std::streamsize>(
                values_.size() * sizeof(values_.front())));
        Require(output.good(), "failed to create SimSink fixture");
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
        {"initial_sequence_id", std::int64_t{0}},
    };
}

class FramePublisher {
public:
    explicit FramePublisher(std::size_t maximum_wire_bytes)
        : payload_(maximum_wire_bytes - sdk::kFrameEnvelopeBytes),
          wire_(maximum_wire_bytes) {
        frame_.payload.reserve(12);
    }

    void publish(fg::PortOut<std::byte>& source,
                 const Fixture& fixture,
                 std::size_t element_offset,
                 std::uint32_t pulses,
                 std::uint64_t sequence,
                 std::uint64_t timestamp,
                 bool corrupt_payload = false,
                 std::uint32_t header_pulses = 0) {
        frame_.header.pulses =
            header_pulses == 0 ? pulses : header_pulses;
        frame_.header.samples_per_pulse = 3;
        frame_.payload.resize(static_cast<std::size_t>(pulses) * 3);
        std::copy_n(
            fixture.values().begin() + element_offset,
            frame_.payload.size(),
            frame_.payload.begin());
        if (corrupt_payload) {
            ++frame_.payload.front().i;
        }
        const std::size_t payload_bytes =
            sdk::FrameDataCodec<IQFrame>::encoded_size(frame_);
        Require(sdk::FrameDataCodec<IQFrame>::encode(
                    frame_,
                    cy::common::Span<std::byte>(
                        payload_.data(), payload_bytes)),
                "SimSink test IQFrame encode failed");
        std::size_t wire_bytes = 0;
        Require(sdk::EncodeFrame(
                    cy::common::Span<const std::byte>(
                        payload_.data(), payload_bytes),
                    {sequence, timestamp},
                    cy::common::Span<std::byte>(
                        wire_.data(), wire_.size()),
                    &wire_bytes),
                "SimSink test frame seal failed");
        publish_bytes(source, wire_.data(), wire_bytes);
    }

    void publish_bytes(fg::PortOut<std::byte>& source,
                       const std::byte* bytes,
                       std::size_t size) {
        std::size_t offset = 0;
        while (offset < size) {
            auto span = source.reserve(size - offset);
            Require(!span.empty(), "SimSink test source backpressured");
            std::memcpy(span.data(), bytes + offset, span.size());
            offset += span.size();
            span.commit(span.size());
        }
    }

private:
    IQFrame frame_;
    std::vector<std::byte> payload_;
    std::vector<std::byte> wire_;
};

void TestValidationAndContinue() {
    Fixture fixture;
    SimSink sink(Params(fixture));
    fg::PortOut<std::byte> source;
    fg::connect(source, sink.in, sink.maximum_wire_bytes() * 2);
    FramePublisher publisher(sink.maximum_wire_bytes());

    publisher.publish(source, fixture, 0, 4, 0, 100);
    Require(sink.process_work(), "SimSink did not consume valid frame");
    publisher.publish(source, fixture, 12, 4, 99, 101);
    Require(sink.process_work(), "SimSink did not consume bad sequence");
    publisher.publish(source, fixture, 24, 2, 2, 100);
    Require(sink.process_work(), "SimSink did not consume bad timestamp");
    publisher.publish(source, fixture, 0, 4, 3, 102, false, 3);
    Require(sink.process_work(), "SimSink did not consume bad header");
    publisher.publish(source, fixture, 12, 4, 4, 103, true);
    Require(sink.process_work(), "SimSink did not consume bad payload");
    publisher.publish(source, fixture, 24, 2, 5, 104);
    Require(sink.process_work(), "SimSink did not recover after errors");

    const auto& stats = sink.stats();
    Require(stats.frames_received == 6, "SimSink received count mismatch");
    Require(stats.frames_valid == 2, "SimSink valid count mismatch");
    Require(stats.sequence_errors == 1, "SimSink sequence count mismatch");
    Require(stats.timestamp_errors == 1, "SimSink timestamp count mismatch");
    Require(stats.header_errors == 1, "SimSink header count mismatch");
    Require(stats.payload_errors == 1, "SimSink payload count mismatch");
}

void TestPartialFrameDoesNotConsume() {
    Fixture fixture;
    SimSink sink(Params(fixture));
    fg::PortOut<std::byte> source;
    fg::connect(source, sink.in, sink.maximum_wire_bytes());
    FramePublisher publisher(sink.maximum_wire_bytes());

    IQFrame frame;
    frame.header = {4, 3};
    frame.payload.assign(fixture.values().begin(),
                         fixture.values().begin() + 12);
    std::vector<std::byte> payload(
        sdk::FrameDataCodec<IQFrame>::encoded_size(frame));
    Require(sdk::FrameDataCodec<IQFrame>::encode(
                frame,
                cy::common::Span<std::byte>(
                    payload.data(), payload.size())),
            "partial test encode failed");
    std::vector<std::byte> wire(sdk::WireFrameBytes(payload.size()));
    Require(sdk::EncodeFrame(
                cy::common::Span<const std::byte>(
                    payload.data(), payload.size()),
                {0, 100},
                cy::common::Span<std::byte>(
                    wire.data(), wire.size())),
            "partial test seal failed");

    for (std::size_t split = 1; split < wire.size(); ++split) {
        SimSink split_sink(Params(fixture));
        fg::PortOut<std::byte> split_source;
        fg::connect(
            split_source, split_sink.in, split_sink.maximum_wire_bytes());
        publisher.publish_bytes(split_source, wire.data(), split);
        const auto available = split_sink.in.available();
        Require(!split_sink.process_work(),
                "SimSink consumed an incomplete frame");
        Require(split_sink.in.available() == available,
                "SimSink advanced the residual-frame cursor");
        publisher.publish_bytes(
            split_source, wire.data() + split, wire.size() - split);
        Require(split_sink.process_work(),
                "SimSink rejected a completed split frame");
        Require(split_sink.stats().frames_valid == 1,
                "SimSink split frame was not valid");
    }
}

void TestFramingAndCodecErrors() {
    Fixture fixture;
    SimSink sink(Params(fixture));
    fg::PortOut<std::byte> source;
    fg::connect(source, sink.in, sink.maximum_wire_bytes() * 2);
    FramePublisher publisher(sink.maximum_wire_bytes());

    std::array<std::byte, sdk::kFrameEnvelopeBytes> garbage{};
    publisher.publish_bytes(source, garbage.data(), garbage.size());
    Require(sink.process_work(), "SimSink did not resync bad magic");
    Require(sink.stats().framing_errors == 1,
            "SimSink framing error count mismatch");
    Require(sink.in.consume_exact(garbage.size() - 1),
            "SimSink test failed to remove residual garbage");

    std::array<std::byte, sizeof(uestcradar::nodes::IQFrameHeader) + 1>
        malformed_payload{};
    std::vector<std::byte> malformed_wire(
        sdk::WireFrameBytes(malformed_payload.size()));
    Require(sdk::EncodeFrame(
                cy::common::Span<const std::byte>(
                    malformed_payload.data(), malformed_payload.size()),
                {0, 100},
                cy::common::Span<std::byte>(
                    malformed_wire.data(), malformed_wire.size())),
            "SimSink malformed codec frame seal failed");
    publisher.publish_bytes(
        source, malformed_wire.data(), malformed_wire.size());
    Require(sink.process_work(), "SimSink did not consume codec error");
    Require(sink.stats().codec_errors == 1,
            "SimSink codec error count mismatch");

    publisher.publish(source, fixture, 12, 4, 1, 101);
    Require(sink.process_work(), "SimSink did not recover after codec error");
    Require(sink.stats().frames_valid == 1,
            "SimSink valid frame did not recover after codec error");
}

void TestZeroAllocations() {
    Fixture fixture;
    SimSink sink(Params(fixture));
    fg::PortOut<std::byte> source;
    fg::connect(source, sink.in, sink.maximum_wire_bytes());
    FramePublisher publisher(sink.maximum_wire_bytes());

    const std::size_t offsets[3] = {0, 12, 24};
    const std::uint32_t pulses[3] = {4, 4, 2};
    for (std::size_t i = 0; i < 3; ++i) {
        publisher.publish(source, fixture, offsets[i], pulses[i], i, 100 + i);
        Require(sink.process_work(), "SimSink warmup failed");
    }

    cycore::benchmark::detail::begin_allocation_tracking();
    for (std::size_t i = 0; i < 30; ++i) {
        const std::size_t frame = i % 3;
        publisher.publish(
            source, fixture, offsets[frame], pulses[frame],
            i + 3, i + 103);
        Require(sink.process_work(), "SimSink steady-state work failed");
    }
    const std::size_t allocations =
        cycore::benchmark::detail::end_allocation_tracking();
    Require(allocations == 0,
            "SimSink allocated memory during steady-state work");
}

void TestInvalidConfig() {
    Fixture fixture;
    auto params = Params(fixture);
    params["samples_per_pulse"] = std::int64_t{0};
    bool threw = false;
    try {
        SimSink sink(params);
        (void)sink;
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    Require(threw, "SimSink accepted zero samples_per_pulse");

    params = Params(fixture);
    params["file_path"] = fixture.path() + ".missing.bin";
    threw = false;
    try {
        SimSink sink(params);
        (void)sink;
    } catch (const std::runtime_error&) {
        threw = true;
    }
    Require(threw, "SimSink accepted a missing reference file");
}

} // namespace

CYCORE_BENCHMARK_DEFINE_ALLOCATION_OPERATORS()

int main() {
    TestValidationAndContinue();
    TestPartialFrameDoesNotConsume();
    TestFramingAndCodecErrors();
    TestZeroAllocations();
    TestInvalidConfig();
    return 0;
}
