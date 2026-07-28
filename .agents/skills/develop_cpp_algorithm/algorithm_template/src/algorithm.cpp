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
    const std::size_t sample_count = input.header.sample_count;
    if (sample_count == 0 ||
        sample_count > my_block_data::kMaxSamples ||
        input.payload.size() != sample_count) {
        return cycore::sdk::ProcessResult::Drop;
    }
    output.header.sample_count = input.header.sample_count;
    output.payload.resize(sample_count);
    for (std::size_t i = 0; i < sample_count; ++i) {
        output.payload[i] =
            input.payload[i] * static_cast<float>(factor_);
    }
    return cycore::sdk::ProcessResult::Produced;
}
