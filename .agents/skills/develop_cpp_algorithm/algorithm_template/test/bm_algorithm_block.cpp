#include <cycore_benchmark_harness.h>

#include "algorithm.h"

#include <cstddef>

CYCORE_REGISTER_BENCHMARK(
    MyAlgorithm,
    [](MyAlgorithm::InputData& input) {
        input.header.sample_count =
            cycore::algorithm::my_block::kMaxSamples;
        input.payload.resize(input.header.sample_count);
        for (std::size_t i = 0; i < input.payload.size(); ++i) {
            input.payload[i] = static_cast<float>(i) * 0.25F;
        }
    });
