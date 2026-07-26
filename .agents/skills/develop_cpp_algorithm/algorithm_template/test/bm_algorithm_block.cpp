#include "algorithm.h"
#include "block_test_harness.h"
#include "data.h"

#include <cycore_algorithm_sdk.h>
#include <flowgraph/block_wrapper.h>
#include <flowgraph/value.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace fg = cy::flowgraph;
namespace sdk = cycore::sdk;
namespace data = cycore::algorithm::my_block;
namespace test_support = algorithm_template::test;

namespace {

struct BlockBenchmarkConfig {
    std::string name;
    std::size_t input_elements_per_work = 0;
    std::size_t output_elements_per_work = 0;
    std::size_t input_capacity = 0;
    std::size_t output_capacity = 0;
    std::size_t warmup_works = 200;
    std::size_t minimum_measured_works = 1000;
    double minimum_measurement_seconds = 1.0;

    // Logical input item rate required by the deployment. It is used only to
    // report measured/target headroom and never throttles the saturation test.
    std::optional<double> target_input_mitems_per_second;
};

struct BlockBenchmarkResult {
    bool passed = false;
    std::string error;
    std::uint64_t successful_works = 0;
    std::uint64_t failed_works = 0;
    std::uint64_t input_elements = 0;
    std::uint64_t output_elements = 0;
    double measured_work_seconds = 0.0;
    std::vector<double> latency_us;
};

template <typename InputSample, typename OutputSample>
struct BlockBenchmarkCase {
    BlockBenchmarkConfig config;
    std::vector<InputSample> input_frame;
    std::unique_ptr<fg::BlockModel> (*make_block)();
};

std::size_t ParsePositiveSize(const char* text, const char* name) {
    const auto parsed = std::stoull(text);
    if (parsed == 0 || parsed > std::numeric_limits<std::size_t>::max()) {
        throw std::invalid_argument(std::string(name) + " must be positive");
    }
    return static_cast<std::size_t>(parsed);
}

double Percentile(const std::vector<double>& sorted_values, double percentile) {
    if (sorted_values.empty()) {
        return 0.0;
    }
    const double scaled =
        (percentile / 100.0) * static_cast<double>(sorted_values.size() - 1);
    return sorted_values[static_cast<std::size_t>(scaled)];
}

void ValidateConfig(const BlockBenchmarkConfig& config) {
    if (config.name.empty()) {
        throw std::invalid_argument("benchmark case name must not be empty");
    }
    if (config.input_elements_per_work == 0 ||
        config.output_elements_per_work == 0) {
        throw std::invalid_argument(
            "benchmark requires positive fixed input and output transaction sizes");
    }
    if (config.input_capacity < config.input_elements_per_work ||
        config.output_capacity < config.output_elements_per_work) {
        throw std::invalid_argument(
            "ring capacity must hold at least one complete transaction");
    }
    if (config.warmup_works == 0 || config.minimum_measured_works == 0 ||
        config.minimum_measurement_seconds <= 0.0) {
        throw std::invalid_argument(
            "warmup count, minimum measured count, and measurement duration must be positive");
    }
    if (config.target_input_mitems_per_second &&
        *config.target_input_mitems_per_second <= 0.0) {
        throw std::invalid_argument("target input rate must be positive");
    }
}

template <typename InputSample, typename OutputSample>
test_support::WorkObservation RunPreparedWork(
    test_support::BlockTestHarness<InputSample, OutputSample>& harness,
    const BlockBenchmarkConfig& config,
    const std::vector<InputSample>& input_frame) {
    if (harness.output_available() != 0) {
        throw std::runtime_error("previous output transaction was not drained");
    }
    if (harness.output_writable() < config.output_elements_per_work) {
        throw std::runtime_error("output ring is backpressured before work()");
    }

    harness.publish(input_frame);
    if (harness.input_available() < config.input_elements_per_work) {
        throw std::runtime_error("input ring is starved before work()");
    }

    // BlockTestHarness times only the production BlockModel::work() call.
    const auto observation = harness.work_once();
    if (!observation.succeeded) {
        throw std::runtime_error("work() failed despite prepared input/output rings");
    }
    if (observation.consumed_input_elements != config.input_elements_per_work ||
        observation.produced_output_elements != config.output_elements_per_work) {
        throw std::runtime_error("work() violated the configured fixed N-to-M contract");
    }

    (void)harness.drain_one_transaction();
    return observation;
}

template <typename InputSample, typename OutputSample>
BlockBenchmarkResult RunCase(
    const BlockBenchmarkCase<InputSample, OutputSample>& benchmark_case) {
    BlockBenchmarkResult result;

    try {
        const auto& config = benchmark_case.config;
        ValidateConfig(config);
        if (!benchmark_case.make_block) {
            throw std::invalid_argument("benchmark BlockModel factory is missing");
        }
        if (benchmark_case.input_frame.size() !=
            config.input_elements_per_work) {
            throw std::invalid_argument(
                "pre-generated input frame size does not match the configured input count");
        }

        test_support::BlockTestHarness<InputSample, OutputSample> harness(
            benchmark_case.make_block(),
            config.input_elements_per_work,
            config.output_elements_per_work,
            config.input_capacity,
            config.output_capacity);

        for (std::size_t i = 0; i < config.warmup_works; ++i) {
            (void)RunPreparedWork(harness, config, benchmark_case.input_frame);
        }

        result.latency_us.reserve(config.minimum_measured_works);
        while (result.successful_works < config.minimum_measured_works ||
               result.measured_work_seconds < config.minimum_measurement_seconds) {
            try {
                const auto observation =
                    RunPreparedWork(harness, config, benchmark_case.input_frame);
                ++result.successful_works;
                result.input_elements += observation.consumed_input_elements;
                result.output_elements += observation.produced_output_elements;
                result.measured_work_seconds +=
                    std::chrono::duration<double>(observation.latency).count();
                result.latency_us.push_back(
                    std::chrono::duration<double, std::micro>(
                        observation.latency).count());
            } catch (...) {
                ++result.failed_works;
                throw;
            }
        }

        if (result.successful_works < config.minimum_measured_works ||
            result.measured_work_seconds < config.minimum_measurement_seconds ||
            result.failed_works != 0 ||
            result.measured_work_seconds <= 0.0) {
            throw std::runtime_error("benchmark did not complete every measured work");
        }
        result.passed = true;
    } catch (const std::exception& ex) {
        result.error = ex.what();
    }

    return result;
}

template <typename InputSample, typename OutputSample>
void PrintResult(const BlockBenchmarkCase<InputSample, OutputSample>& benchmark_case,
                 BlockBenchmarkResult result) {
    const auto& config = benchmark_case.config;

    std::cout << "\nComplete Block saturation limit: " << config.name << '\n'
              << "  scope=BlockModel::work only; excludes device/network/disk/scheduler\n"
              << "  input_type_key="
              << fg::detail::type_key<InputSample>()
              << " input_item_size=" << sizeof(InputSample)
              << " output_type_key="
              << fg::detail::type_key<OutputSample>()
              << " output_item_size=" << sizeof(OutputSample) << '\n'
              << "  input_elements_per_work=" << config.input_elements_per_work
              << " output_elements_per_work=" << config.output_elements_per_work
              << " input_bytes_per_work="
              << config.input_elements_per_work * sizeof(InputSample)
              << " output_bytes_per_work="
              << config.output_elements_per_work * sizeof(OutputSample) << '\n'
              << "  input_capacity=" << config.input_capacity
              << " output_capacity=" << config.output_capacity
              << " warmup_works=" << config.warmup_works
              << " minimum_measured_works=" << config.minimum_measured_works
              << " minimum_measurement_seconds="
              << config.minimum_measurement_seconds << '\n';

    if (!result.passed) {
        std::cout << "  status=FAILED"
                  << " successful_works=" << result.successful_works
                  << " failed_works=" << result.failed_works
                  << " error=\"" << result.error << "\"\n";
        return;
    }

    std::sort(result.latency_us.begin(), result.latency_us.end());
    const double average_us =
        std::accumulate(result.latency_us.begin(), result.latency_us.end(), 0.0) /
        static_cast<double>(result.latency_us.size());
    const double input_mitems_per_second =
        static_cast<double>(result.input_elements) /
        result.measured_work_seconds / 1.0e6;
    const double output_mitems_per_second =
        static_cast<double>(result.output_elements) /
        result.measured_work_seconds / 1.0e6;
    const double input_gb_per_second =
        static_cast<double>(result.input_elements * sizeof(InputSample)) /
        result.measured_work_seconds / 1.0e9;
    const double output_gb_per_second =
        static_cast<double>(result.output_elements * sizeof(OutputSample)) /
        result.measured_work_seconds / 1.0e9;
    const double aggregate_gb_per_second =
        (static_cast<double>(result.input_elements * sizeof(InputSample)) +
         static_cast<double>(result.output_elements * sizeof(OutputSample))) /
        result.measured_work_seconds / 1.0e9;

    std::cout << std::fixed << std::setprecision(3)
              << "  status=PASSED"
              << " successful_works=" << result.successful_works
              << " failed_works=" << result.failed_works
              << " measured_work_seconds=" << result.measured_work_seconds << '\n'
              << "  actual_input_elements=" << result.input_elements
              << " actual_output_elements=" << result.output_elements << '\n'
              << "  input_Mitems/s=" << input_mitems_per_second
              << " output_Mitems/s=" << output_mitems_per_second << '\n'
              << "  input_GB/s=" << input_gb_per_second
              << " output_GB/s=" << output_gb_per_second
              << " aggregate_IO_GB/s=" << aggregate_gb_per_second << '\n'
              << "  latency_us avg=" << average_us
              << " p50=" << Percentile(result.latency_us, 50.0)
              << " p95=" << Percentile(result.latency_us, 95.0)
              << " p99=" << Percentile(result.latency_us, 99.0)
              << " max=" << result.latency_us.back() << '\n';

    if (config.target_input_mitems_per_second) {
        std::cout << "  target_input_Mitems/s="
                  << *config.target_input_mitems_per_second
                  << " realtime_headroom="
                  << input_mitems_per_second /
                         *config.target_input_mitems_per_second
                  << "x\n";
    }
}

using ProductionBlock =
    sdk::AlgorithmBlockAdapter<MyAlgorithm, data::InputSample, data::OutputSample>;

std::unique_ptr<fg::BlockModel> MakeProductionBlock() {
    fg::ValueMap params;
    params["factor"] = 1.25;
    return std::unique_ptr<fg::BlockModel>(
        new fg::BlockWrapper<ProductionBlock>(
            "benchmark_algorithm", fg::BlockTypeName{"algorithm.my_block"}, params));
}

BlockBenchmarkCase<data::InputSample, data::OutputSample> MakeDefaultCase(
    std::size_t minimum_measured_works,
    std::size_t warmup_works,
    double minimum_measurement_seconds) {
    BlockBenchmarkCase<data::InputSample, data::OutputSample> benchmark_case;
    benchmark_case.config = BlockBenchmarkConfig{
        "algorithm.my_block/default",
        data::kInputElementsPerWork,
        data::kOutputElementsPerWork,
        data::kInputElementsPerWork * 4,
        data::kOutputElementsPerWork * 4,
        warmup_works,
        minimum_measured_works,
        minimum_measurement_seconds,
        std::nullopt};
    benchmark_case.input_frame.resize(data::kInputElementsPerWork);
    for (std::size_t i = 0; i < benchmark_case.input_frame.size(); ++i) {
        benchmark_case.input_frame[i] =
            static_cast<data::InputSample>(static_cast<double>(i % 257) / 257.0);
    }
    benchmark_case.make_block = &MakeProductionBlock;
    return benchmark_case;
}

} // namespace

int main(int argc, char** argv) {
    const std::size_t measurement_milliseconds =
        argc > 1 ? ParsePositiveSize(argv[1], "measurement milliseconds") : 1000;
    const std::size_t warmup_works =
        argc > 2 ? ParsePositiveSize(argv[2], "warmup works") : 200;
    const std::size_t minimum_measured_works =
        argc > 3 ? ParsePositiveSize(argv[3], "minimum measured works") : 1000;

    const auto benchmark_case = MakeDefaultCase(
        minimum_measured_works,
        warmup_works,
        static_cast<double>(measurement_milliseconds) / 1000.0);
    auto result = RunCase(benchmark_case);
    PrintResult(benchmark_case, result);
    return result.passed ? 0 : 1;
}
