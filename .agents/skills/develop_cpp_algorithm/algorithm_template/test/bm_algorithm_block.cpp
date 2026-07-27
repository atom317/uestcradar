#include <cycore_benchmark_harness.h>

#include "algorithm.h"

#include <cstddef>

CYCORE_REGISTER_BENCHMARK(
    MyAlgorithm,
    [](MyAlgorithm::InputData& input) {
        input.sample_count = cycore::algorithm::my_block::kMaxSamples;
        for (std::size_t i = 0; i < input.sample_count; ++i) {
            input.samples[i] = static_cast<float>(i) * 0.25F;
        }
    });
