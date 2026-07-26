#include "range_doppler_algorithm.h"

#include <block_benchmark_runner.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace data = cycore::algorithm::range_doppler;
namespace test_support = cycore::sdk::test;

namespace {

using InputSample = data::InputSample;
using OutputSample = data::OutputSample;
using BenchmarkCase = test_support::BlockBenchmarkCase<InputSample>;

// Algorithm developers edit only this function.  N is the complete input cube;
// M is the one-channel Doppler/range output cube produced by this Block.
std::vector<BenchmarkCase> MakeBenchmarkCases() {
    BenchmarkCase small;
    small.name = "algorithm.range_doppler/1x8x64";
    small.params["num_channels"] = static_cast<std::int64_t>(1);
    small.params["num_pulses"] = static_cast<std::int64_t>(8);
    small.params["samples_per_pulse"] = static_cast<std::int64_t>(64);
    small.N = 1 * 8 * 64;
    small.M = 8 * 64;

    BenchmarkCase dual_channel;
    dual_channel.name = "algorithm.range_doppler/2x8x64";
    dual_channel.params["num_channels"] = static_cast<std::int64_t>(2);
    dual_channel.params["num_pulses"] = static_cast<std::int64_t>(8);
    dual_channel.params["samples_per_pulse"] = static_cast<std::int64_t>(64);
    dual_channel.N = 2 * 8 * 64;
    dual_channel.M = 8 * 64;

    BenchmarkCase medium;
    medium.name = "algorithm.range_doppler/2x32x256";
    medium.params["num_channels"] = static_cast<std::int64_t>(2);
    medium.params["num_pulses"] = static_cast<std::int64_t>(32);
    medium.params["samples_per_pulse"] = static_cast<std::int64_t>(256);
    medium.N = 2 * 32 * 256;
    medium.M = 32 * 256;

    return {std::move(small), std::move(dual_channel), std::move(medium)};
}

} // namespace

int main(int argc, char** argv) {
    return test_support::RunBlockBenchmarks<
        RangeDopplerAlgorithm,
        InputSample,
        OutputSample>(
        argc,
        argv,
        "benchmark_range_doppler",
        "algorithm.range_doppler",
        MakeBenchmarkCases());
}
