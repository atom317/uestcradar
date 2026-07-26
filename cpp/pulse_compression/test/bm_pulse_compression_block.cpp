#include "pulse_compression_algorithm.h"

#include <block_test_harness.h>
#include <cycore_algorithm_sdk.h>
#include <flowgraph/block_wrapper.h>
#include <flowgraph/value.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace fg = cy::flowgraph;
namespace sdk = cycore::sdk;
namespace data = cycore::algorithm::pulse_compression;
namespace test_support = cycore::sdk::test;

namespace {

using InputSample = data::InputSample;
using OutputSample = data::OutputSample;
using ProductionBlock =
    sdk::AlgorithmBlockAdapter<PulseCompressionAlgorithm, InputSample, OutputSample>;
using Harness = test_support::BlockTestHarness<InputSample, OutputSample>;

struct Case {
    std::string name;
    std::size_t channels;
    std::size_t pulses;
    std::size_t samples;
    std::optional<double> target_input_mitems_per_second;
};

std::size_t Elements(const Case& c) { return c.channels * c.pulses * c.samples; }

fg::ValueMap MakeParams(const Case& c) {
    fg::ValueMap params;
    params["num_channels"] = static_cast<std::int64_t>(c.channels);
    params["num_pulses"] = static_cast<std::int64_t>(c.pulses);
    params["samples_per_pulse"] = static_cast<std::int64_t>(c.samples);
    return params;
}

std::unique_ptr<fg::BlockModel> MakeBlock(const Case& c) {
    return std::unique_ptr<fg::BlockModel>(
        new fg::BlockWrapper<ProductionBlock>(
            "benchmark_pulse_compression",
            fg::BlockTypeName{"algorithm.pulse_compression"}, MakeParams(c)));
}

std::vector<InputSample> MakeInput(const Case& c) {
    std::vector<InputSample> input(Elements(c));
    std::uint32_t state = 0x2468ace1U;
    for (auto& sample : input) {
        state = state * 1664525U + 1013904223U;
        sample.i = static_cast<std::int16_t>(state >> 16U);
        state = state * 1664525U + 1013904223U;
        sample.q = static_cast<std::int16_t>(state >> 16U);
    }
    return input;
}

double Percentile(const std::vector<double>& sorted, double percentile) {
    return sorted[static_cast<std::size_t>(
        (percentile / 100.0) * static_cast<double>(sorted.size() - 1))];
}

void RunCase(const Case& c, std::size_t minimum_works, std::size_t warmup_works,
             double minimum_seconds) {
    const std::size_t n = Elements(c);
    const std::size_t m = n;
    Harness harness(MakeBlock(c), n, m, n * 2, m * 2);
    const auto input = MakeInput(c);
    for (std::size_t i = 0; i < warmup_works; ++i) {
        harness.publish(input);
        const auto observation = harness.work_once();
        if (!observation.succeeded || observation.consumed_input_elements != n ||
            observation.produced_output_elements != m) {
            throw std::runtime_error(c.name + " failed during warmup");
        }
        (void)harness.drain_one_transaction();
    }

    std::vector<double> latency_us;
    latency_us.reserve(minimum_works);
    std::uint64_t successful_works = 0;
    std::uint64_t failed_works = 0;
    double measured_seconds = 0.0;
    while (successful_works < minimum_works || measured_seconds < minimum_seconds) {
        harness.publish(input);
        const auto observation = harness.work_once();
        if (!observation.succeeded || observation.consumed_input_elements != n ||
            observation.produced_output_elements != m) {
            ++failed_works;
            throw std::runtime_error(c.name + " violated its N-to-M transaction contract");
        }
        (void)harness.drain_one_transaction();
        const double latency = std::chrono::duration<double, std::micro>(observation.latency).count();
        latency_us.push_back(latency);
        measured_seconds += latency / 1.0e6;
        ++successful_works;
    }

    std::sort(latency_us.begin(), latency_us.end());
    const double items = static_cast<double>(successful_works * n);
    const double mitems = items / measured_seconds / 1.0e6;
    const double gbps = items * sizeof(InputSample) / measured_seconds / 1.0e9;
    const double average = std::accumulate(latency_us.begin(), latency_us.end(), 0.0) /
                           static_cast<double>(latency_us.size());
    std::cout << std::fixed << std::setprecision(3)
              << "\nComplete Block saturation limit: " << c.name << '\n'
              << "  input_type=" << fg::detail::type_key<InputSample>()
              << " input_item_size=" << sizeof(InputSample)
              << " output_type=" << fg::detail::type_key<OutputSample>()
              << " output_item_size=" << sizeof(OutputSample) << '\n'
              << "  input_elements_per_work=" << n
              << " output_elements_per_work=" << m
              << " input_bytes_per_work=" << n * sizeof(InputSample)
              << " output_bytes_per_work=" << m * sizeof(OutputSample) << '\n'
              << "  status=PASSED successful_works=" << successful_works
              << " failed_works=" << failed_works << '\n'
              << "  input_Mitems/s=" << mitems << " output_Mitems/s=" << mitems << '\n'
              << "  input_GB/s=" << gbps << " output_GB/s=" << gbps
              << " aggregate_IO_GB/s=" << 2.0 * gbps << '\n'
              << "  latency_us avg=" << average
              << " p50=" << Percentile(latency_us, 50.0)
              << " p95=" << Percentile(latency_us, 95.0)
              << " p99=" << Percentile(latency_us, 99.0)
              << " max=" << latency_us.back() << '\n';
    if (c.target_input_mitems_per_second) {
        std::cout << "  target_input_Mitems/s=" << *c.target_input_mitems_per_second
                  << " realtime_headroom=" << mitems / *c.target_input_mitems_per_second << "x\n";
    }
}

std::size_t ParsePositive(const char* text, const char* name) {
    const auto value = std::stoull(text);
    if (value == 0) throw std::invalid_argument(std::string(name) + " must be positive");
    return static_cast<std::size_t>(value);
}

} // namespace

int main(int argc, char** argv) {
    try {
        const std::size_t milliseconds = argc > 1 ? ParsePositive(argv[1], "measurement milliseconds") : 1000;
        const std::size_t warmup = argc > 2 ? ParsePositive(argv[2], "warmup works") : 200;
        const std::size_t minimum_works = argc > 3 ? ParsePositive(argv[3], "minimum measured works") : 1000;
        const std::vector<Case> cases{
            {"algorithm.pulse_compression/1x1x256", 1, 1, 256, std::nullopt},
            {"algorithm.pulse_compression/1x1x1024", 1, 1, 1024, std::nullopt},
            {"algorithm.pulse_compression/2x4x4096", 2, 4, 4096, std::nullopt},
        };
        for (const auto& c : cases) {
            RunCase(c, minimum_works, warmup, static_cast<double>(milliseconds) / 1000.0);
        }
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "bm_pulse_compression_block failed: " << ex.what() << '\n';
        return 1;
    }
}
