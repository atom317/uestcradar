#include <cycore_benchmark_harness.h>

#include "pulse_compression_algorithm.h"

#include <cmath>
#include <cstddef>

namespace data = cycore::algorithm::pulse_compression;

CYCORE_REGISTER_BENCHMARK(
    PulseCompressionAlgorithm,
    [](PulseCompressionAlgorithm::InputData& input) {
        constexpr double kPi = 3.14159265358979323846;
        constexpr double kSampleRate = 30.72e6;
        constexpr double kBandwidth = 20.0e6;
        constexpr double kPulseDuration =
            static_cast<double>(data::kReplicaPoints) /
            kSampleRate;
        constexpr double kStartFrequency = -10.0e6;
        constexpr std::size_t kDelay = 137;
        constexpr double kAmplitude = 4096.0;

        input.header.points = data::kDefaultPoints;
        input.payload.assign(
            input.header.points,
            cy::common::CS16{0, 0});
        const double chirp_slope =
            kBandwidth / kPulseDuration;
        for (std::size_t sample = 0;
             sample < data::kReplicaPoints;
             ++sample) {
            const double time =
                static_cast<double>(sample) / kSampleRate;
            const double phase =
                2.0 * kPi * kStartFrequency * time +
                kPi * chirp_slope * time * time;
            input.payload[kDelay + sample] = {
                static_cast<std::int16_t>(
                    std::lround(kAmplitude *
                                std::cos(phase))),
                static_cast<std::int16_t>(
                    std::lround(kAmplitude *
                                std::sin(phase))),
            };
        }
    });
