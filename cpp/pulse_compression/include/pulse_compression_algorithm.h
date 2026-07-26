#pragma once

#include "data.h"

#include <cycore_algorithm_sdk.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace pulse_compression_detail {
class FftwBatchBackend;
}

namespace pulse_compression_data = cycore::algorithm::pulse_compression;

// Production algorithm contract.  The FFT implementation is deliberately
// private so the plugin ABI, Params, and Reader/Writer interface stay stable.
class PulseCompressionAlgorithm {
public:
    explicit PulseCompressionAlgorithm(const cycore::sdk::Params& params);
    ~PulseCompressionAlgorithm();

    PulseCompressionAlgorithm(const PulseCompressionAlgorithm&) = delete;
    PulseCompressionAlgorithm& operator=(const PulseCompressionAlgorithm&) = delete;

    bool work(cycore::sdk::Reader<pulse_compression_data::InputSample>& in,
              cycore::sdk::Writer<pulse_compression_data::OutputSample>& out);

private:
    static constexpr std::size_t kReplicaLength = 256;

    static std::size_t ReadSizeParam(const cycore::sdk::Params& params,
                                     const std::string& key,
                                     std::size_t fallback);
    static std::int16_t ClampS16(float value);

    std::size_t num_channels_;
    std::size_t num_pulses_;
    std::size_t samples_per_pulse_;
    std::unique_ptr<pulse_compression_detail::FftwBatchBackend> fft_;
};
