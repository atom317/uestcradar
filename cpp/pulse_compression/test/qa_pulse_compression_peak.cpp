#include <pulse_compression_algorithm.h>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <cmath>

#include <flowgraph/port.h>
#include <flowgraph/value.h>

namespace fg = cy::flowgraph;

using InputSample = cycore::algorithm::pulse_compression::InputSample;
using OutputSample = cycore::algorithm::pulse_compression::OutputSample;

std::size_t CubeIndex(std::size_t channel_count,
                      std::size_t samples_per_pulse,
                      std::size_t channel,
                      std::size_t pulse,
                      std::size_t sample) {
    return ((pulse * samples_per_pulse + sample) * channel_count) + channel;
}

void TestPeak(std::size_t target_range_bin) {
    const std::size_t channel_count = 1;
    const std::size_t pulses = 1;
    const std::size_t samples_per_pulse = 4096;
    const std::size_t element_count = channel_count * pulses * samples_per_pulse;

    fg::ValueMap params;
    params["num_channels"] = static_cast<std::int64_t>(channel_count);
    params["num_pulses"] = static_cast<std::int64_t>(pulses);
    params["samples_per_pulse"] = static_cast<std::int64_t>(samples_per_pulse);

    fg::PortOut<InputSample> source;
    cycore::sdk::AlgorithmBlockAdapter<PulseCompressionAlgorithm, InputSample, OutputSample> block(params);
    fg::PortIn<OutputSample> sink;
    fg::connect(source, block.in, element_count * 2);
    fg::connect(block.out, sink, element_count * 2);

    auto input = source.reserve(element_count);
    assert(input.size() == element_count);

    // Initialize to zero
    for(std::size_t i = 0; i < element_count; ++i) {
        input[i] = InputSample{0, 0};
    }

    constexpr double kSampleRate = 30.72e6;
    constexpr double kPulseWidth = 256.0 / kSampleRate;
    constexpr double kBandwidth = 20e6;
    constexpr double kChirpSlope = kBandwidth / kPulseWidth;
    constexpr double kStartFreq = -10e6;
    constexpr double PI = 3.14159265358979323846;

    for (std::size_t i = 0; i < 256; ++i) {
        double t = static_cast<double>(i) / kSampleRate;
        double phase = 2.0 * PI * kStartFreq * t + PI * kChirpSlope * t * t;
        double cos_val = std::cos(phase) * 32767.0;
        double sin_val = std::sin(phase) * 32767.0;
        
        std::size_t sample = target_range_bin + i;
        if (sample < samples_per_pulse) {
            std::size_t idx = CubeIndex(channel_count, samples_per_pulse, 0, 0, sample);
            input[idx] = InputSample{
                static_cast<std::int16_t>(cos_val),
                static_cast<std::int16_t>(sin_val)
            };
        }
    }
    input.commit(element_count);

    block.work();
    auto output = sink.get(element_count);
    assert(output.size() == element_count);

    std::size_t peak_sample = 0;
    double max_mag_sq = -1.0;
    for (std::size_t sample = 0; sample < samples_per_pulse; ++sample) {
        std::size_t idx = CubeIndex(channel_count, samples_per_pulse, 0, 0, sample);
        auto out_val = output[idx];
        double mag_sq = static_cast<double>(out_val.i)*out_val.i + static_cast<double>(out_val.q)*out_val.q;
        if (mag_sq > max_mag_sq) {
            max_mag_sq = mag_sq;
            peak_sample = sample;
        }
    }

    (void)input;
    output.consume(element_count);

    std::cout << "Testing target_range_bin = " << target_range_bin << std::endl;
    std::cout << "Peak found at sample: " << peak_sample << std::endl;
    if (peak_sample != target_range_bin) {
        std::cout << "ERROR: Peak offset is " << (static_cast<int>(peak_sample) - static_cast<int>(target_range_bin)) << std::endl;
    } else {
        std::cout << "SUCCESS: Peak matches target_range_bin." << std::endl;
    }
    std::cout << "---------------------------------------" << std::endl;
}

int main() {
    TestPeak(0);
    TestPeak(37);
    TestPeak(4096 - 256);
    return 0;
}
