#include "range_doppler_algorithm.h"

#include <block_test_harness.h>
#include <cycore_algorithm_sdk.h>
#include <flowgraph/block_wrapper.h>
#include <flowgraph/value.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace fg = cy::flowgraph;
namespace sdk = cycore::sdk;
namespace test_support = cycore::sdk::test;
namespace data = cycore::algorithm::range_doppler;

namespace {

using InputSample = data::InputSample;
using OutputSample = data::OutputSample;
using ProductionBlock =
    sdk::AlgorithmBlockAdapter<RangeDopplerAlgorithm, InputSample, OutputSample>;
using Harness = test_support::BlockTestHarness<InputSample, OutputSample>;

struct Dimensions {
    std::size_t channels;
    std::size_t pulses;
    std::size_t samples;

    std::size_t input_elements() const { return channels * pulses * samples; }
    std::size_t output_elements() const { return pulses * samples; }
};

void Require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

fg::ValueMap MakeParams(const Dimensions& dimensions) {
    fg::ValueMap params;
    params["num_channels"] = static_cast<std::int64_t>(dimensions.channels);
    params["num_pulses"] = static_cast<std::int64_t>(dimensions.pulses);
    params["samples_per_pulse"] = static_cast<std::int64_t>(dimensions.samples);
    return params;
}

std::unique_ptr<fg::BlockModel> MakeBlock(const Dimensions& dimensions) {
    return std::unique_ptr<fg::BlockModel>(
        new fg::BlockWrapper<ProductionBlock>(
            "range_doppler_test", fg::BlockTypeName{"algorithm.range_doppler"},
            MakeParams(dimensions)));
}

std::size_t InputIndex(const Dimensions& dimensions,
                       std::size_t channel,
                       std::size_t pulse,
                       std::size_t sample) {
    return ((pulse * dimensions.samples + sample) * dimensions.channels) + channel;
}

std::size_t OutputIndex(const Dimensions& dimensions,
                        std::size_t doppler_bin,
                        std::size_t sample) {
    return doppler_bin * dimensions.samples + sample;
}

std::vector<InputSample> MakeTone(const Dimensions& dimensions,
                                  std::size_t range_bin,
                                  int doppler_bin,
                                  std::int16_t amplitude,
                                  bool excite_other_channels) {
    std::vector<InputSample> input(dimensions.input_elements(), InputSample{0, 0});
    constexpr double kPi = 3.14159265358979323846;
    for (std::size_t pulse = 0; pulse < dimensions.pulses; ++pulse) {
        const double phase = 2.0 * kPi * static_cast<double>(doppler_bin) *
                             static_cast<double>(pulse) /
                             static_cast<double>(dimensions.pulses);
        input[InputIndex(dimensions, 0, pulse, range_bin)] = InputSample{
            static_cast<std::int16_t>(std::lround(amplitude * std::cos(phase))),
            static_cast<std::int16_t>(std::lround(amplitude * std::sin(phase)))};
        if (excite_other_channels) {
            for (std::size_t channel = 1; channel < dimensions.channels; ++channel) {
                input[InputIndex(dimensions, channel, pulse, range_bin)] = InputSample{
                    static_cast<std::int16_t>((pulse & 1U) ? -12000 : 12000),
                    static_cast<std::int16_t>((channel & 1U) ? 7000 : -7000)};
            }
        }
    }
    return input;
}

std::vector<OutputSample> RunFrame(const Dimensions& dimensions,
                                   const std::vector<InputSample>& input) {
    Harness harness(MakeBlock(dimensions), dimensions.input_elements(),
                    dimensions.output_elements(), dimensions.input_elements() * 2,
                    dimensions.output_elements() * 2);
    harness.publish(input);
    const auto observation = harness.work_once();
    Require(observation.succeeded, "Range-Doppler complete transaction failed");
    Require(observation.consumed_input_elements == dimensions.input_elements(),
            "Range-Doppler consumed unexpected input count");
    Require(observation.produced_output_elements == dimensions.output_elements(),
            "Range-Doppler produced unexpected output count");
    return harness.drain_one_transaction();
}

void VerifyTone(const Dimensions& dimensions,
                std::size_t range_bin,
                int doppler_bin,
                std::int16_t amplitude) {
    const auto output = RunFrame(
        dimensions, MakeTone(dimensions, range_bin, doppler_bin, amplitude, false));
    int unshifted = doppler_bin;
    if (unshifted < 0) {
        unshifted += static_cast<int>(dimensions.pulses);
    }
    const std::size_t shifted =
        static_cast<std::size_t>((unshifted + dimensions.pulses / 2) % dimensions.pulses);
    const double expected_db = 10.0 * std::log10(
        static_cast<double>(dimensions.pulses) * amplitude * amplitude);

    std::size_t measured_peak = 0;
    for (std::size_t bin = 1; bin < dimensions.pulses; ++bin) {
        if (output[OutputIndex(dimensions, bin, range_bin)] >
            output[OutputIndex(dimensions, measured_peak, range_bin)]) {
            measured_peak = bin;
        }
    }
    Require(measured_peak == shifted, "fftshift frequency-bin alignment mismatch");
    Require(std::abs(output[OutputIndex(dimensions, shifted, range_bin)] - expected_db) < 0.25,
            "coherent FFT gain/dB conversion mismatch");
    for (std::size_t bin = 0; bin < dimensions.pulses; ++bin) {
        if (bin != shifted) {
            Require(output[OutputIndex(dimensions, bin, range_bin)] < expected_db - 20.0,
                    "integer Doppler tone leaked excessively into another bin");
        }
    }
}

void TestFrequencyAndFftShift() {
    const Dimensions dimensions{2, 8, 16};
    VerifyTone(dimensions, 5, 0, 1000);       // DC must land at P / 2.
    VerifyTone(dimensions, 5, 3, 1000);
    VerifyTone(dimensions, 5, -2, 1000);
    VerifyTone(dimensions, 5, 4, 1000);       // Nyquist.
}

void TestCurrentSingleOutputChannelContract() {
    const Dimensions dimensions{3, 8, 16};
    const auto reference = RunFrame(dimensions, MakeTone(dimensions, 4, 1, 900, false));
    const auto with_other_channels =
        RunFrame(dimensions, MakeTone(dimensions, 4, 1, 900, true));
    Require(reference.size() == dimensions.output_elements(),
            "Range-Doppler output must contain one channel only");
    for (std::size_t i = 0; i < reference.size(); ++i) {
        Require(std::abs(reference[i] - with_other_channels[i]) < 1.0e-5f,
                "non-zero input channel changed channel-0-only RD output");
    }
}

void TestInputShortageAndOutputBackpressure() {
    const Dimensions dimensions{2, 8, 16};
    const auto frame = MakeTone(dimensions, 2, 1, 1000, false);

    {
        Harness harness(MakeBlock(dimensions), dimensions.input_elements(),
                        dimensions.output_elements(), dimensions.input_elements() * 2,
                        dimensions.output_elements() * 2);
        harness.publish(frame.data(), frame.size() - 1);
        const auto observation = harness.work_once();
        Require(!observation.succeeded && observation.consumed_input_elements == 0 &&
                    observation.produced_output_elements == 0,
                "insufficient RD input must roll back the whole transaction");
    }
    {
        Harness harness(MakeBlock(dimensions), dimensions.input_elements(),
                        dimensions.output_elements(), dimensions.input_elements() * 2,
                        dimensions.output_elements() * 2);
        harness.publish(frame);
        Require(harness.work_once().succeeded, "first RD frame should fill output ring");
        harness.publish(frame);
        Require(harness.work_once().succeeded, "second RD frame should fill output ring");
        harness.publish(frame);
        const auto observation = harness.work_once();
        Require(!observation.succeeded && observation.consumed_input_elements == 0 &&
                    observation.produced_output_elements == 0,
                "RD output backpressure must roll back the whole transaction");
    }
}

void TestRingWrapAndParameterBoundary() {
    const Dimensions dimensions{2, 8, 16};
    Harness harness(MakeBlock(dimensions), dimensions.input_elements(),
                    dimensions.output_elements(), dimensions.input_elements() * 2,
                    dimensions.output_elements() * 2);
    for (int bin = 0; bin < 6; ++bin) {
        harness.publish(MakeTone(dimensions, 3, bin % 4, 700, false));
        Require(harness.work_once().succeeded, "RD ring-wrap transaction failed");
        Require(harness.drain_one_transaction().size() == dimensions.output_elements(),
                "RD ring-wrap output size mismatch");
    }

    fg::ValueMap invalid = MakeParams(dimensions);
    invalid["num_pulses"] = static_cast<std::int64_t>(6);
    bool rejected = false;
    try {
        ProductionBlock invalid_block(invalid);
        (void)invalid_block;
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    Require(rejected, "RD must reject non-power-of-two pulse counts");
}

} // namespace

int main() {
    try {
        TestFrequencyAndFftShift();
        TestCurrentSingleOutputChannelContract();
        TestInputShortageAndOutputBackpressure();
        TestRingWrapAndParameterBoundary();
        std::cout << "qa_range_doppler_block passed\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "qa_range_doppler_block failed: " << ex.what() << '\n';
        return 1;
    }
}
