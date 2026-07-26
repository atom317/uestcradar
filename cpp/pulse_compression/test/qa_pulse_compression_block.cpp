#include "pulse_compression_algorithm.h"

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
#include <type_traits>
#include <utility>
#include <vector>

namespace fg = cy::flowgraph;
namespace sdk = cycore::sdk;
namespace test_support = cycore::sdk::test;
namespace data = cycore::algorithm::pulse_compression;

namespace {

using InputSample = data::InputSample;
using OutputSample = data::OutputSample;
using ProductionBlock =
    sdk::AlgorithmBlockAdapter<PulseCompressionAlgorithm, InputSample, OutputSample>;
using Harness = test_support::BlockTestHarness<InputSample, OutputSample>;

constexpr std::size_t kReplicaLength = 256;

struct Dimensions {
    std::size_t channels;
    std::size_t pulses;
    std::size_t samples;
    std::size_t elements() const { return channels * pulses * samples; }
};

void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
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
            "pulse_compression_test", fg::BlockTypeName{"algorithm.pulse_compression"},
            MakeParams(dimensions)));
}

std::size_t Index(const Dimensions& dimensions,
                  std::size_t channel,
                  std::size_t pulse,
                  std::size_t sample) {
    return ((pulse * dimensions.samples + sample) * dimensions.channels) + channel;
}

struct ComplexReference { double re; double im; };

std::vector<ComplexReference> MakeReplica() {
    constexpr double kSampleRate = 30.72e6;
    constexpr double kPulseWidth = 256.0 / kSampleRate;
    constexpr double kBandwidth = 20e6;
    constexpr double kSlope = kBandwidth / kPulseWidth;
    constexpr double kStartFrequency = -10e6;
    constexpr double kPi = 3.14159265358979323846;
    std::vector<ComplexReference> replica(kReplicaLength);
    for (std::size_t i = 0; i < replica.size(); ++i) {
        const double t = static_cast<double>(i) / kSampleRate;
        const double phase = 2.0 * kPi * kStartFrequency * t + kPi * kSlope * t * t;
        replica[i] = {std::cos(phase), std::sin(phase)};
    }
    return replica;
}

std::int16_t ClampS16(double value) {
    if (value >= 32767.0) return 32767;
    if (value <= -32768.0) return -32768;
    return static_cast<std::int16_t>(std::round(value));
}

std::vector<InputSample> MakeDelayedEchoes(const Dimensions& dimensions,
                                           const std::vector<std::size_t>& delays,
                                           std::int16_t amplitude) {
    Require(delays.size() == dimensions.channels, "one delay per channel is required");
    const auto replica = MakeReplica();
    std::vector<InputSample> input(dimensions.elements(), InputSample{0, 0});
    for (std::size_t channel = 0; channel < dimensions.channels; ++channel) {
        Require(delays[channel] + kReplicaLength <= dimensions.samples,
                "echo does not fit inside the configured PRI");
        for (std::size_t pulse = 0; pulse < dimensions.pulses; ++pulse) {
            for (std::size_t m = 0; m < kReplicaLength; ++m) {
                input[Index(dimensions, channel, pulse, delays[channel] + m)] = InputSample{
                    ClampS16(static_cast<double>(amplitude) * replica[m].re),
                    ClampS16(static_cast<double>(amplitude) * replica[m].im)};
            }
        }
    }
    return input;
}

std::vector<OutputSample> ReferenceCompress(const Dimensions& dimensions,
                                             const std::vector<InputSample>& input) {
    const auto replica = MakeReplica();
    std::vector<OutputSample> output(dimensions.elements());
    for (std::size_t pulse = 0; pulse < dimensions.pulses; ++pulse) {
        for (std::size_t sample = 0; sample < dimensions.samples; ++sample) {
            for (std::size_t channel = 0; channel < dimensions.channels; ++channel) {
                double sum_i = 0.0;
                double sum_q = 0.0;
                for (std::size_t m = 0; m < kReplicaLength && sample + m < dimensions.samples; ++m) {
                    const auto x = input[Index(dimensions, channel, pulse, sample + m)];
                    sum_i += static_cast<double>(x.i) * replica[m].re +
                             static_cast<double>(x.q) * replica[m].im;
                    sum_q += static_cast<double>(x.q) * replica[m].re -
                             static_cast<double>(x.i) * replica[m].im;
                }
                output[Index(dimensions, channel, pulse, sample)] = OutputSample{
                    ClampS16(sum_i / static_cast<double>(kReplicaLength)),
                    ClampS16(sum_q / static_cast<double>(kReplicaLength))};
            }
        }
    }
    return output;
}

std::vector<OutputSample> RunFrame(const Dimensions& dimensions,
                                   const std::vector<InputSample>& input) {
    Harness harness(MakeBlock(dimensions), dimensions.elements(), dimensions.elements(),
                    dimensions.elements() * 2, dimensions.elements() * 2);
    harness.publish(input);
    const auto observation = harness.work_once();
    Require(observation.succeeded && observation.consumed_input_elements == dimensions.elements() &&
                observation.produced_output_elements == dimensions.elements(),
            "Pulse-compression transaction count mismatch");
    return harness.drain_one_transaction();
}

double MagnitudeSquared(const OutputSample& sample) {
    return static_cast<double>(sample.i) * sample.i + static_cast<double>(sample.q) * sample.q;
}

void TestReferenceAccuracyPeakAndPslr() {
    const Dimensions dimensions{1, 1, 512};
    const std::size_t delay = 71;
    const auto input = MakeDelayedEchoes(dimensions, {delay}, 4096);
    const auto actual = RunFrame(dimensions, input);
    const auto expected = ReferenceCompress(dimensions, input);
    for (std::size_t i = 0; i < actual.size(); ++i) {
        Require(std::abs(static_cast<int>(actual[i].i) - expected[i].i) <= 1 &&
                    std::abs(static_cast<int>(actual[i].q) - expected[i].q) <= 1,
                "CS16 matched-filter output differs from independent reference");
    }

    std::size_t peak = 0;
    double peak_power = -1.0;
    double sidelobe_power = 0.0;
    double expected_peak_power = -1.0;
    double expected_sidelobe_power = 0.0;
    for (std::size_t sample = 0; sample < dimensions.samples; ++sample) {
        const double power = MagnitudeSquared(actual[Index(dimensions, 0, 0, sample)]);
        if (power > peak_power) {
            peak_power = power;
            peak = sample;
        }
        const double expected_power = MagnitudeSquared(expected[Index(dimensions, 0, 0, sample)]);
        expected_peak_power = std::max(expected_peak_power, expected_power);
    }
    Require(peak == delay, "matched-filter peak does not map to the target range bin");
    for (std::size_t sample = 0; sample < dimensions.samples; ++sample) {
        if (sample != peak) {
            sidelobe_power = std::max(
                sidelobe_power, MagnitudeSquared(actual[Index(dimensions, 0, 0, sample)]));
            expected_sidelobe_power = std::max(
                expected_sidelobe_power,
                MagnitudeSquared(expected[Index(dimensions, 0, 0, sample)]));
        }
    }
    const double pslr_db = 10.0 * std::log10(std::max(sidelobe_power, 1.0) / peak_power);
    const double expected_pslr_db =
        10.0 * std::log10(std::max(expected_sidelobe_power, 1.0) / expected_peak_power);
    Require(std::abs(pslr_db - expected_pslr_db) < 0.01,
            "matched-filter PSLR differs from independent reference");
}

void TestDelayEdgesAndChannelIsolation() {
    const Dimensions dimensions{2, 2, 512};
    const auto input = MakeDelayedEchoes(dimensions, {0, 256}, 3500);
    const auto actual = RunFrame(dimensions, input);
    const auto expected = ReferenceCompress(dimensions, input);
    for (std::size_t i = 0; i < actual.size(); ++i) {
        Require(std::abs(static_cast<int>(actual[i].i) - expected[i].i) <= 1 &&
                    std::abs(static_cast<int>(actual[i].q) - expected[i].q) <= 1,
                "multi-channel pulse-compression reference mismatch");
    }
    for (std::size_t pulse = 0; pulse < dimensions.pulses; ++pulse) {
        for (std::size_t channel = 0; channel < dimensions.channels; ++channel) {
            const std::size_t expected_delay = channel == 0 ? 0 : 256;
            std::size_t peak = 0;
            double peak_power = -1.0;
            for (std::size_t sample = 0; sample < dimensions.samples; ++sample) {
                const double power = MagnitudeSquared(actual[Index(dimensions, channel, pulse, sample)]);
                if (power > peak_power) { peak_power = power; peak = sample; }
            }
            Require(peak == expected_delay, "channel/pulse matched-filter peak is not isolated");
        }
    }
}

void TestReferenceAcrossFftSizes() {
    const std::vector<std::pair<Dimensions, std::size_t>> cases{
        {{1, 1, 256}, 0},
        {{1, 1, 1024}, 384},
        {{1, 1, 4096}, 3800},
    };
    for (const auto& test_case : cases) {
        const auto& dimensions = test_case.first;
        const auto input = MakeDelayedEchoes(dimensions, {test_case.second}, 3000);
        const auto actual = RunFrame(dimensions, input);
        const auto expected = ReferenceCompress(dimensions, input);
        for (std::size_t i = 0; i < actual.size(); ++i) {
            Require(std::abs(static_cast<int>(actual[i].i) - expected[i].i) <= 1 &&
                        std::abs(static_cast<int>(actual[i].q) - expected[i].q) <= 1,
                    "NFFT-dependent pulse-compression output differs from reference");
        }
        std::size_t peak = 0;
        double peak_power = -1.0;
        for (std::size_t sample = 0; sample < dimensions.samples; ++sample) {
            const double power = MagnitudeSquared(actual[Index(dimensions, 0, 0, sample)]);
            if (power > peak_power) {
                peak_power = power;
                peak = sample;
            }
        }
        Require(peak == test_case.second, "NFFT-dependent peak position mismatch");
    }
}

void TestTransactionalBoundariesAndRingWrap() {
    const Dimensions dimensions{1, 1, 512};
    const auto frame = MakeDelayedEchoes(dimensions, {37}, 3000);
    {
        Harness harness(MakeBlock(dimensions), dimensions.elements(), dimensions.elements(),
                        dimensions.elements() * 2, dimensions.elements() * 2);
        harness.publish(frame.data(), frame.size() - 1);
        const auto observation = harness.work_once();
        Require(!observation.succeeded && observation.consumed_input_elements == 0 &&
                    observation.produced_output_elements == 0,
                "insufficient pulse-compression input must roll back");
    }
    {
        Harness harness(MakeBlock(dimensions), dimensions.elements(), dimensions.elements(),
                        dimensions.elements() * 2, dimensions.elements() * 2);
        harness.publish(frame);
        Require(harness.work_once().succeeded,
                "first pulse-compression frame should fill output ring");
        harness.publish(frame);
        Require(harness.work_once().succeeded,
                "second pulse-compression frame should fill output ring");
        harness.publish(frame);
        const auto observation = harness.work_once();
        Require(!observation.succeeded && observation.consumed_input_elements == 0 &&
                    observation.produced_output_elements == 0,
                "pulse-compression output backpressure must roll back");
    }
    {
        Harness harness(MakeBlock(dimensions), dimensions.elements(), dimensions.elements(),
                        dimensions.elements() * 2, dimensions.elements() * 2);
        for (std::size_t i = 0; i < 5; ++i) {
            harness.publish(frame);
            Require(harness.work_once().succeeded, "pulse-compression ring-wrap transaction failed");
            Require(harness.drain_one_transaction().size() == dimensions.elements(),
                    "pulse-compression ring-wrap output size mismatch");
        }
    }
}

void TestCurrentCs16ContractAndParameterBoundary() {
    static_assert(std::is_same<InputSample, OutputSample>::value,
                  "current production contract is CS16 to CS16");
    const Dimensions dimensions{1, 1, 512};
    fg::ValueMap invalid = MakeParams(dimensions);
    invalid["num_channels"] = static_cast<std::int64_t>(0);
    bool rejected = false;
    try {
        ProductionBlock invalid_block(invalid);
        (void)invalid_block;
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    Require(rejected, "pulse-compression must reject zero dimensions");
}

} // namespace

int main() {
    try {
        TestReferenceAccuracyPeakAndPslr();
        TestDelayEdgesAndChannelIsolation();
        TestReferenceAcrossFftSizes();
        TestTransactionalBoundariesAndRingWrap();
        TestCurrentCs16ContractAndParameterBoundary();
        std::cout << "qa_pulse_compression_block passed\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "qa_pulse_compression_block failed: " << ex.what() << '\n';
        return 1;
    }
}
