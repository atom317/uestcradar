#include <cycore_benchmark_harness.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

template <std::size_t PayloadBytes>
struct PODFrame {
    std::array<std::byte, PayloadBytes> payload{};
};

template <std::size_t PayloadBytes>
class CopyAlgorithm {
public:
    using InputData = PODFrame<PayloadBytes>;
    using OutputData = PODFrame<PayloadBytes>;

    explicit CopyAlgorithm(const cycore::sdk::Params&) {}

    cycore::sdk::ProcessResult work(
        const InputData& input,
        OutputData& output) noexcept {
        output = input;
        return cycore::sdk::ProcessResult::Produced;
    }
};

struct VectorFrameHeader {
    std::uint64_t payload_bytes = 0;
};

struct VectorFrame {
    VectorFrameHeader header{};
    std::vector<std::byte> payload;
};

static_assert(cycore::sdk::is_vector_frame_v<VectorFrame>);
static_assert(cycore::sdk::is_supported_frame_v<VectorFrame>);

struct UnsupportedVectorFrame {
    VectorFrameHeader metadata{};
    std::vector<std::byte> samples;
};

struct UnsupportedBoolVectorFrame {
    VectorFrameHeader header{};
    std::vector<bool> payload;
};

static_assert(!cycore::sdk::is_supported_frame_v<UnsupportedVectorFrame>);
static_assert(!cycore::sdk::is_supported_frame_v<UnsupportedBoolVectorFrame>);

class VectorCopyAlgorithm {
public:
    using InputData = VectorFrame;
    using OutputData = VectorFrame;

    explicit VectorCopyAlgorithm(const cycore::sdk::Params&) {}

    cycore::sdk::ProcessResult work(
        const InputData& input,
        OutputData& output) noexcept {
        output.header = input.header;
        output.payload.resize(input.payload.size());
        std::copy(input.payload.begin(), input.payload.end(),
                  output.payload.begin());
        return cycore::sdk::ProcessResult::Produced;
    }
};

template <std::size_t PayloadBytes>
bool RunCase(std::size_t minimum_duration_ms) {
    cycore::benchmark::BenchmarkOptions options;
    options.minimum_duration =
        std::chrono::milliseconds(minimum_duration_ms);
    options.warmup_frames = 2;
    options.minimum_frames = 8;

    const std::string name =
        "pod/" + std::to_string(PayloadBytes) + "B";
    const auto result =
        cycore::benchmark::RunFrameBenchmark<
            CopyAlgorithm<PayloadBytes>>(
            name,
            [](PODFrame<PayloadBytes>& input) {
                input.payload.front() = std::byte{0x31};
                input.payload[PayloadBytes / 2] = std::byte{0x52};
                input.payload.back() = std::byte{0x73};
            },
            options);
    cycore::benchmark::PrintBenchmarkResult(result);
    return result.framework_allocations == 0;
}

bool RunVectorCase(std::size_t payload_bytes,
                   std::size_t minimum_duration_ms) {
    cycore::benchmark::BenchmarkOptions options;
    options.minimum_duration =
        std::chrono::milliseconds(minimum_duration_ms);
    options.warmup_frames = 2;
    options.minimum_frames = 8;

    const std::string name =
        "vector/" + std::to_string(payload_bytes) + "B";
    const auto result =
        cycore::benchmark::RunFrameBenchmark<VectorCopyAlgorithm>(
            name,
            [payload_bytes](VectorFrame& input) {
                input.header.payload_bytes = payload_bytes;
                input.payload.resize(payload_bytes);
                input.payload.front() = std::byte{0x31};
                input.payload[payload_bytes / 2] = std::byte{0x52};
                input.payload.back() = std::byte{0x73};
            },
            options);
    cycore::benchmark::PrintBenchmarkResult(result);
    return result.framework_allocations == 0;
}

CYCORE_BENCHMARK_DEFINE_ALLOCATION_OPERATORS()

int main(int argc, char** argv) {
    try {
        if (argc > 2) {
            throw std::invalid_argument(
                "usage: bm_frame_sizes [minimum_duration_ms]");
        }
        const std::size_t duration_ms =
            argc == 2
                ? static_cast<std::size_t>(std::stoull(argv[1]))
                : 250;

        bool zero_allocations = true;
        zero_allocations &= RunCase<64>(duration_ms);
        zero_allocations &= RunCase<1024>(duration_ms);
        zero_allocations &= RunCase<4096>(duration_ms);
        zero_allocations &= RunCase<64 * 1024>(duration_ms);
        zero_allocations &= RunCase<1024 * 1024>(duration_ms);
        zero_allocations &= RunCase<8 * 1024 * 1024>(duration_ms);
        zero_allocations &= RunVectorCase(64, duration_ms);
        zero_allocations &= RunVectorCase(4096, duration_ms);
        zero_allocations &= RunVectorCase(1024 * 1024, duration_ms);
        zero_allocations &= RunVectorCase(8 * 1024 * 1024, duration_ms);
        return zero_allocations ? 0 : 2;
    } catch (const std::exception& error) {
        std::cerr << "frame-size benchmark failed: "
                  << error.what() << '\n';
        return 1;
    }
}
