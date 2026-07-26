#include "pulse_compression_algorithm.h"

#include <block_benchmark_runner.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace data = cycore::algorithm::pulse_compression;
namespace test_support = cycore::sdk::test;

namespace {

using InputSample = data::InputSample;
using OutputSample = data::OutputSample;
using BenchmarkCase = test_support::BlockBenchmarkCase<InputSample>;

// Algorithm developers edit only this function.  Pulse compression preserves
// the complete channels-by-pulses-by-samples element count, so N equals M.
std::vector<BenchmarkCase> MakeBenchmarkCases() {
    BenchmarkCase small;
    small.name = "algorithm.pulse_compression/1x1x256";
    small.params["num_channels"] = static_cast<std::int64_t>(1);
    small.params["num_pulses"] = static_cast<std::int64_t>(1);
    small.params["samples_per_pulse"] = static_cast<std::int64_t>(256);
    small.N = 1 * 1 * 256;
    small.M = 1 * 1 * 256;

    BenchmarkCase short_frame;
    short_frame.name = "algorithm.pulse_compression/1x1x1024";
    short_frame.params["num_channels"] = static_cast<std::int64_t>(1);
    short_frame.params["num_pulses"] = static_cast<std::int64_t>(1);
    short_frame.params["samples_per_pulse"] = static_cast<std::int64_t>(1024);
    short_frame.N = 1 * 1 * 1024;
    short_frame.M = 1 * 1 * 1024;

    BenchmarkCase medium;
    medium.name = "algorithm.pulse_compression/2x4x4096";
    medium.params["num_channels"] = static_cast<std::int64_t>(2);
    medium.params["num_pulses"] = static_cast<std::int64_t>(4);
    medium.params["samples_per_pulse"] = static_cast<std::int64_t>(4096);
    medium.N = 2 * 4 * 4096;
    medium.M = 2 * 4 * 4096;

    BenchmarkCase eight_channel;
    eight_channel.name = "algorithm.pulse_compression/8x4x4096";
    eight_channel.params["num_channels"] = static_cast<std::int64_t>(8);
    eight_channel.params["num_pulses"] = static_cast<std::int64_t>(4);
    eight_channel.params["samples_per_pulse"] = static_cast<std::int64_t>(4096);
    eight_channel.N = 8 * 4 * 4096;
    eight_channel.M = 8 * 4 * 4096;

    BenchmarkCase production;
    production.name = "algorithm.pulse_compression/8x64x4096";
    production.params["num_channels"] = static_cast<std::int64_t>(8);
    production.params["num_pulses"] = static_cast<std::int64_t>(64);
    production.params["samples_per_pulse"] = static_cast<std::int64_t>(4096);
    production.N = 8 * 64 * 4096;
    production.M = 8 * 64 * 4096;
    production.target_input_mitems_per_second = 245.76;

    return {std::move(small),
            std::move(short_frame),
            std::move(medium),
            std::move(eight_channel),
            std::move(production)};
}

} // namespace

int main(int argc, char** argv) {
    return test_support::RunBlockBenchmarks<
        PulseCompressionAlgorithm,
        InputSample,
        OutputSample>(
        argc,
        argv,
        "benchmark_pulse_compression",
        "algorithm.pulse_compression",
        MakeBenchmarkCases());
}
