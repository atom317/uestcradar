#include "algorithm.h"
#include "data.h"

#include <block_benchmark_runner.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace data = cycore::algorithm::my_block;
namespace test_support = cycore::sdk::test;

namespace {

using InputSample = data::InputSample;
using OutputSample = data::OutputSample;
using BenchmarkCase = test_support::BlockBenchmarkCase<InputSample>;

// Algorithm developers edit only this function.  The shared runner creates the
// production BlockModel, chooses safe ring capacities, and owns all timing and
// metric logic.
std::vector<BenchmarkCase> MakeBenchmarkCases() {
    BenchmarkCase benchmark_case;
    benchmark_case.name = "algorithm.my_block/default";
    benchmark_case.params["factor"] = 1.25;
    benchmark_case.N = data::kInputElementsPerWork;
    benchmark_case.M = data::kOutputElementsPerWork;
    return {std::move(benchmark_case)};
}

} // namespace

int main(int argc, char** argv) {
    return test_support::RunBlockBenchmarks<MyAlgorithm, InputSample, OutputSample>(
        argc,
        argv,
        "benchmark_algorithm",
        "algorithm.my_block",
        MakeBenchmarkCases());
}
