#include "algorithm.h"

#include <cycore_algorithm_sdk.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace data = cycore::algorithm::my_block;
namespace fg = cy::flowgraph;
namespace sdk = cycore::sdk;

namespace {

std::size_t ParseSize(const char* text, std::size_t fallback) {
    if (!text) {
        return fallback;
    }
    try {
        return static_cast<std::size_t>(std::stoull(text));
    } catch (...) {
        return fallback;
    }
}

void Publish(fg::PortOut<std::byte>& source,
             const std::vector<std::byte>& wire) {
    std::size_t offset = 0;
    while (offset < wire.size()) {
        auto span = source.reserve(wire.size() - offset);
        if (span.empty()) {
            throw std::runtime_error("benchmark input backpressured");
        }
        std::memcpy(span.data(), wire.data() + offset, span.size());
        offset += span.size();
        span.commit(span.size());
    }
}

} // namespace

int main(int argc, char** argv) {
    constexpr const char* kCaseName = "algorithm.my_block/frame";
    const std::size_t minimum_ms = argc > 1 ? ParseSize(argv[1], 1000) : 1000;
    const std::size_t warmup_calls = argc > 2 ? ParseSize(argv[2], 100) : 100;
    const std::size_t minimum_calls = argc > 3 ? ParseSize(argv[3], 1000) : 1000;
    if (argc > 4 && std::string(kCaseName).find(argv[4]) == std::string::npos) {
        std::cout << "没有匹配的 benchmark case\n";
        return 0;
    }

    data::InputData input;
    input.sample_count = data::kMaxSamples;
    for (std::size_t i = 0; i < input.sample_count; ++i) {
        input.samples[i] = static_cast<float>(i) * 0.25F;
    }
    const std::size_t payload_bytes = sdk::FrameCodec<data::InputData>::encoded_size(input);
    std::vector<std::byte> payload(payload_bytes);
    if (!sdk::FrameCodec<data::InputData>::encode(
            input, cy::common::Span<std::byte>(payload.data(), payload.size()))) {
        return 1;
    }
    std::vector<std::byte> input_wire(sdk::WireFrameBytes(payload.size()));
    if (!sdk::EncodeFrame(
            cy::common::Span<const std::byte>(payload.data(), payload.size()),
            {1, 1},
            cy::common::Span<std::byte>(input_wire.data(), input_wire.size()))) {
        return 1;
    }

    const std::size_t capacity = input_wire.size() * 2 + 17;
    fg::ValueMap params{
        {"factor", 1.25},
        {"max_input_frame_bytes", static_cast<std::int64_t>(capacity)},
        {"max_output_frame_bytes", static_cast<std::int64_t>(capacity)},
    };
    fg::PortOut<std::byte> source;
    sdk::FrameAlgorithmAdapter<MyAlgorithm> adapter(params);
    fg::PortIn<std::byte> sink;
    fg::connect(source, adapter.in, capacity);
    fg::connect(adapter.out, sink, capacity);
    std::vector<std::byte> output_wire(input_wire.size());
    volatile float checksum = 0.0F;

    const auto run_once = [&] {
        Publish(source, input_wire);
        if (!adapter.process_work()) {
            throw std::runtime_error("benchmark frame transaction failed");
        }
        std::size_t output_offset = 0;
        while (adapter.has_pending_output() || sink.available() != 0) {
            const std::size_t chunk = sink.available();
            if (chunk != 0) {
                if (output_offset + chunk > output_wire.size() ||
                    sink.peek_copy(
                        0,
                        cy::common::Span<std::byte>(
                            output_wire.data() + output_offset, chunk)) != chunk) {
                    throw std::runtime_error("benchmark output read failed");
                }
                if (!sink.consume_exact(chunk)) {
                    throw std::runtime_error("benchmark output consume failed");
                }
                output_offset += chunk;
            }
            if (adapter.has_pending_output() && !adapter.process_work()) {
                throw std::runtime_error("benchmark pending output stalled");
            }
        }
        if (output_offset != output_wire.size()) {
            throw std::runtime_error("benchmark output frame size mismatch");
        }
        float sample = 0.0F;
        std::memcpy(&sample,
                    output_wire.data() + sdk::kFrameEnvelopeBytes +
                        sizeof(std::uint32_t) + sizeof(float),
                    sizeof(sample));
        checksum += sample;
    };

    for (std::size_t i = 0; i < warmup_calls; ++i) {
        run_once();
    }

    const auto start = std::chrono::steady_clock::now();
    std::size_t calls = 0;
    do {
        run_once();
        ++calls;
    } while (calls < minimum_calls ||
             std::chrono::steady_clock::now() - start <
                 std::chrono::milliseconds(minimum_ms));
    const double seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - start).count();
    const double frames_per_second = static_cast<double>(calls) / seconds;
    const double gib_per_second =
        frames_per_second * static_cast<double>(payload_bytes) /
        static_cast<double>(std::uint64_t{1} << 30U);

    std::cout << "case=" << kCaseName
              << " calls=" << calls
              << " frames_per_s=" << frames_per_second
              << " payload_gib_per_s=" << gib_per_second
              << " average_latency_us=" << 1.0e6 / frames_per_second
              << " checksum=" << checksum << '\n';
    return 0;
}
