#pragma once

#include "data.h"

#include <cycore_algorithm_sdk.h>

#include <cstddef>
#include <cstdint>
#include <memory>

namespace pulse_compression_detail {
class PulseCompressionImplementation;
}

namespace pulse_compression_data = cycore::algorithm::pulse_compression;

class PulseCompressionAlgorithm {
public:
    using InputData = pulse_compression_data::InputData;
    using OutputData = pulse_compression_data::OutputData;

    explicit PulseCompressionAlgorithm(const cycore::sdk::Params& params);
    ~PulseCompressionAlgorithm();

    PulseCompressionAlgorithm(const PulseCompressionAlgorithm&) = delete;
    PulseCompressionAlgorithm& operator=(const PulseCompressionAlgorithm&) = delete;

    cycore::sdk::ProcessResult work(const InputData& input,
                                    OutputData& output) noexcept;

    static cy::flowgraph::ValueMap benchmark_params();

private:
    static std::size_t ReadSizeParam(const cycore::sdk::Params& params,
                                     const char* key,
                                     std::size_t fallback);
    static double ReadPositiveParam(const cycore::sdk::Params& params,
                                    const char* key,
                                    double fallback);

    std::size_t points_;
    std::unique_ptr<pulse_compression_detail::PulseCompressionImplementation> implementation_;
};
