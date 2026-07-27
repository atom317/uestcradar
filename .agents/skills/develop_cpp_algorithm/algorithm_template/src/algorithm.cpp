#include "algorithm.h"

#include <cstddef>
#include <stdexcept>

namespace my_block_data = cycore::algorithm::my_block;

MyAlgorithm::MyAlgorithm(const cycore::sdk::Params& params)
    : factor_(params.get<double>("factor", 1.0)) {
    if (factor_ < -1.0e9 || factor_ > 1.0e9) {
        throw std::invalid_argument("factor is out of supported range");
    }
}

cycore::sdk::ProcessResult MyAlgorithm::work(const InputData& input,
                                             OutputData& output) noexcept {
    if (input.sample_count == 0 || input.sample_count > my_block_data::kMaxSamples) {
        return cycore::sdk::ProcessResult::Drop;
    }
    output.sample_count = input.sample_count;
    for (std::size_t i = 0; i < input.sample_count; ++i) {
        output.samples[i] = input.samples[i] * static_cast<float>(factor_);
    }
    return cycore::sdk::ProcessResult::Produced;
}
