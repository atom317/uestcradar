#pragma once

#include "data.h"

#include <cycore_algorithm_sdk.h>

class MyAlgorithm {
public:
    using InputData = cycore::algorithm::my_block::InputData;
    using OutputData = cycore::algorithm::my_block::OutputData;

    explicit MyAlgorithm(const cycore::sdk::Params& params);

    cycore::sdk::ProcessResult work(const InputData& input,
                                    OutputData& output) noexcept;

private:
    double factor_ = 1.0;
};
