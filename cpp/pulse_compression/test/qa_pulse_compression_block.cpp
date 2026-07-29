#include "pulse_compression_algorithm.h"

#include <cycore_algorithm_sdk.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace data = cycore::algorithm::pulse_compression;
namespace fg = cy::flowgraph;
namespace sdk = cycore::sdk;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kSampleRate = 30.72e6;
constexpr double kBandwidth = 20.0e6;
constexpr double kPulseDuration =
    static_cast<double>(data::kReplicaPoints) / kSampleRate;
constexpr double kStartFrequency = -10.0e6;
constexpr std::size_t kPoints = data::kDefaultPoints;
constexpr std::size_t kReplicaPoints = data::kReplicaPoints;
constexpr std::size_t kMaximumInputWireBytes =
    sdk::kFrameEnvelopeBytes +
    sizeof(data::PulseCompressionHeader) +
    kPoints * sizeof(cy::common::CS16);
constexpr std::size_t kMaximumOutputWireBytes =
    kMaximumInputWireBytes;

struct ComplexReference {
    double i;
    double q;
};

void Require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

fg::ValueMap MakeParams() {
    return {
        {"points", static_cast<std::int64_t>(kPoints)},
        {"replica_points",
         static_cast<std::int64_t>(kReplicaPoints)},
        {"samp_rate", kSampleRate},
        {"bandwidth", kBandwidth},
        {"pulse_duration", kPulseDuration},
        {"start_frequency", kStartFrequency},
        {"max_input_frame_bytes",
         static_cast<std::int64_t>(kMaximumInputWireBytes)},
        {"max_output_frame_bytes",
         static_cast<std::int64_t>(kMaximumOutputWireBytes)},
    };
}

std::vector<ComplexReference> MakeReplica() {
    std::vector<ComplexReference> replica(kReplicaPoints);
    const double chirp_slope = kBandwidth / kPulseDuration;
    for (std::size_t sample = 0; sample < replica.size(); ++sample) {
        const double time = static_cast<double>(sample) / kSampleRate;
        const double phase =
            2.0 * kPi * kStartFrequency * time +
            kPi * chirp_slope * time * time;
        replica[sample] = {std::cos(phase), std::sin(phase)};
    }
    return replica;
}

std::int16_t ClampS16(double value) {
    if (value >= 32767.0) {
        return 32767;
    }
    if (value <= -32768.0) {
        return -32768;
    }
    return static_cast<std::int16_t>(std::round(value));
}

data::InputData MakeDelayedEcho(std::size_t delay,
                                std::int16_t amplitude) {
    Require(delay + kReplicaPoints <= kPoints,
            "test echo does not fit in the frame");
    data::InputData input;
    input.header.points = static_cast<std::uint32_t>(kPoints);
    input.payload.assign(kPoints, cy::common::CS16{0, 0});
    const auto replica = MakeReplica();
    for (std::size_t sample = 0; sample < replica.size(); ++sample) {
        input.payload[delay + sample] = {
            ClampS16(amplitude * replica[sample].i),
            ClampS16(amplitude * replica[sample].q),
        };
    }
    return input;
}

data::OutputData ReferenceCompress(const data::InputData& input) {
    const auto replica = MakeReplica();
    data::OutputData output;
    output.header.points = input.header.points;
    output.payload.resize(kPoints);
    for (std::size_t range = 0; range < kPoints; ++range) {
        double sum_i = 0.0;
        double sum_q = 0.0;
        for (std::size_t tap = 0;
             tap < kReplicaPoints && range + tap < kPoints;
             ++tap) {
            const auto value = input.payload[range + tap];
            sum_i +=
                static_cast<double>(value.i) * replica[tap].i +
                static_cast<double>(value.q) * replica[tap].q;
            sum_q +=
                static_cast<double>(value.q) * replica[tap].i -
                static_cast<double>(value.i) * replica[tap].q;
        }
        output.payload[range] = {
            ClampS16(sum_i / static_cast<double>(kReplicaPoints)),
            ClampS16(sum_q / static_cast<double>(kReplicaPoints)),
        };
    }
    return output;
}

data::OutputData RunDirect(const data::InputData& input) {
    const auto params = MakeParams();
    PulseCompressionAlgorithm algorithm{sdk::Params(params)};
    data::OutputData output;
    output.payload.reserve(kPoints);
    Require(
        algorithm.work(input, output) ==
            sdk::ProcessResult::Produced,
        "valid frame was not produced");
    return output;
}

double MagnitudeSquared(const cy::common::CS16& value) {
    return static_cast<double>(value.i) * value.i +
           static_cast<double>(value.q) * value.q;
}

std::size_t PeakIndex(const data::OutputData& output) {
    return static_cast<std::size_t>(
        std::distance(
            output.payload.begin(),
            std::max_element(
                output.payload.begin(),
                output.payload.end(),
                [](const auto& lhs, const auto& rhs) {
                    return MagnitudeSquared(lhs) <
                           MagnitudeSquared(rhs);
                })));
}

template <typename Frame>
std::vector<std::byte> EncodePayload(const Frame& frame) {
    using Codec = sdk::FrameDataCodec<Frame>;
    std::vector<std::byte> payload(Codec::encoded_size(frame));
    Require(
        Codec::encode(
            frame,
            cy::common::Span<std::byte>(
                payload.data(), payload.size())),
        "failed to encode business frame");
    return payload;
}

std::vector<std::byte> EncodeInput(
    const data::InputData& input,
    sdk::FrameMetadata metadata) {
    const auto payload = EncodePayload(input);
    std::vector<std::byte> wire(
        sdk::WireFrameBytes(payload.size()));
    Require(
        sdk::EncodeFrame(
            cy::common::Span<const std::byte>(
                payload.data(), payload.size()),
            metadata,
            cy::common::Span<std::byte>(
                wire.data(), wire.size())),
        "failed to encode SDK frame");
    return wire;
}

void Publish(fg::PortOut<std::byte>& source,
             const std::byte* bytes,
             std::size_t size) {
    std::size_t offset = 0;
    while (offset < size) {
        auto span = source.reserve(size - offset);
        Require(!span.empty(), "test source was backpressured");
        std::memcpy(span.data(), bytes + offset, span.size());
        offset += span.size();
        span.commit(span.size());
    }
}

std::vector<std::byte> Drain(fg::PortIn<std::byte>& sink) {
    std::vector<std::byte> result(sink.available());
    if (!result.empty()) {
        Require(
            sink.peek_copy(
                0,
                cy::common::Span<std::byte>(
                    result.data(), result.size())) == result.size(),
            "failed to peek output");
        Require(
            sink.consume_exact(result.size()),
            "failed to consume output");
    }
    return result;
}

void TestContractGate() {
    const auto params = MakeParams();
    PulseCompressionAlgorithm algorithm{sdk::Params(params)};
    data::OutputData output;
    output.payload.reserve(kPoints);

    data::InputData empty;
    Require(
        algorithm.work(empty, output) ==
            sdk::ProcessResult::Drop,
        "empty frame was not dropped");

    auto mismatch = MakeDelayedEcho(10, 1000);
    mismatch.header.points =
        static_cast<std::uint32_t>(kPoints - 1);
    Require(
        algorithm.work(mismatch, output) ==
            sdk::ProcessResult::Drop,
        "header/payload mismatch was not dropped");

    auto short_payload = MakeDelayedEcho(10, 1000);
    short_payload.payload.pop_back();
    Require(
        algorithm.work(short_payload, output) ==
            sdk::ProcessResult::Drop,
        "short payload was not dropped");

    auto invalid_params = MakeParams();
    invalid_params["points"] = static_cast<std::int64_t>(0);
    bool rejected = false;
    try {
        PulseCompressionAlgorithm invalid{
            sdk::Params(invalid_params)};
        (void)invalid;
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    Require(rejected, "zero configured points were accepted");
}

void TestGoldenReferencePeakGainAndDelay() {
    constexpr std::int16_t kAmplitude = 4096;
    for (const std::size_t delay :
         {std::size_t{0}, std::size_t{137},
          kPoints - kReplicaPoints}) {
        const auto input = MakeDelayedEcho(delay, kAmplitude);
        const auto actual = RunDirect(input);
        const auto expected = ReferenceCompress(input);

        Require(actual.header.points == kPoints,
                "output header points mismatch");
        Require(actual.payload.size() == kPoints,
                "output payload length mismatch");
        for (std::size_t sample = 0; sample < kPoints; ++sample) {
            Require(
                std::abs(
                    static_cast<int>(actual.payload[sample].i) -
                    expected.payload[sample].i) <= 1 &&
                    std::abs(
                        static_cast<int>(
                            actual.payload[sample].q) -
                        expected.payload[sample].q) <= 1,
                "FFT output differs from time-domain reference at " +
                    std::to_string(sample));
        }

        Require(PeakIndex(actual) == delay,
                "matched-filter peak delay mismatch");
        const double normalized_gain =
            std::sqrt(MagnitudeSquared(actual.payload[delay])) /
            static_cast<double>(kAmplitude);
        Require(
            std::abs(normalized_gain - 1.0) < 0.01,
            "normalized coherent peak gain mismatch");
    }
}

void TestFrameAdapterFragmentAndMetadata() {
    const auto input = MakeDelayedEcho(83, 3000);
    const auto expected = RunDirect(input);
    constexpr sdk::FrameMetadata kMetadata{77, 123456789};
    const auto wire = EncodeInput(input, kMetadata);

    fg::PortOut<std::byte> source;
    sdk::FrameAlgorithmAdapter<PulseCompressionAlgorithm> adapter{
        MakeParams()};
    fg::PortIn<std::byte> sink;
    fg::connect(source, adapter.in, kMaximumInputWireBytes);
    fg::connect(adapter.out, sink, kMaximumOutputWireBytes);

    const std::size_t first = 17;
    const std::size_t second = sdk::kFrameEnvelopeBytes + 9;
    Publish(source, wire.data(), first);
    Require(!adapter.process_work(),
            "partial envelope triggered the algorithm");
    Require(adapter.in.available() == first,
            "partial envelope was consumed");

    Publish(source, wire.data() + first, second - first);
    Require(!adapter.process_work(),
            "partial payload triggered the algorithm");
    Require(adapter.in.available() == second,
            "partial payload was consumed");

    Publish(
        source,
        wire.data() + second,
        wire.size() - second);
    Require(adapter.process_work(),
            "complete frame did not trigger the algorithm");
    Require(adapter.stats().frames_processed == 1 &&
                adapter.stats().frames_emitted == 1,
            "complete frame transaction count mismatch");

    const auto output_wire = Drain(sink);
    const auto inspection = sdk::InspectFrame(
        cy::common::Span<const std::byte>(
            output_wire.data(), output_wire.size()),
        kMaximumOutputWireBytes);
    Require(
        inspection.status ==
            sdk::FrameParseStatus::CompleteFrame,
        "adapter output is not a complete frame");
    Require(
        inspection.metadata.sequence_id ==
                kMetadata.sequence_id &&
            inspection.metadata.timestamp_unix_nano ==
                kMetadata.timestamp_unix_nano,
        "SDK metadata was not propagated");

    data::OutputData decoded;
    decoded.payload.reserve(kPoints);
    Require(
        sdk::FrameDataCodec<data::OutputData>::decode(
            cy::common::Span<const std::byte>(
                output_wire.data() +
                    sdk::kFrameEnvelopeBytes,
                inspection.payload_bytes),
            decoded),
        "adapter output payload decode failed");
    Require(decoded.header.points == kPoints &&
                decoded.payload == expected.payload,
            "adapter output differs from direct output");
}

} // namespace

int main() {
    try {
        static_assert(
            sdk::is_vector_frame_v<data::InputData>,
            "InputData must use SDK header + vector framing");
        static_assert(
            sdk::is_vector_frame_v<data::OutputData>,
            "OutputData must use SDK header + vector framing");
        static_assert(
            kMaximumInputWireBytes == 4132,
            "1024-point CS16 frame must occupy 4132 wire bytes");

        TestContractGate();
        TestGoldenReferencePeakGainAndDelay();
        TestFrameAdapterFragmentAndMetadata();
        std::cout << "qa_pulse_compression_block passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "qa_pulse_compression_block failed: "
                  << error.what() << '\n';
        return 1;
    }
}
