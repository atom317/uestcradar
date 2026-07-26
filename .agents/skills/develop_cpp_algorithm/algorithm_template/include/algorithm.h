#pragma once

#include "data.h"

#include <cycore_algorithm_sdk.h>

class MyAlgorithm {
public:
    explicit MyAlgorithm(const cycore::sdk::Params& params);

    bool work(
        cycore::sdk::Reader<cycore::algorithm::my_block::InputSample>& in,
        cycore::sdk::Writer<cycore::algorithm::my_block::OutputSample>& out);

private:
    double factor_ = 1.0;
};
