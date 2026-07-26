#pragma once

#include <block_test_harness.h>
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
#include <utility>
#include <vector>

namespace cycore::sdk::test {

// The only algorithm-specific benchmark input.  N and M describe one complete
// successful transaction; Params are passed unchanged to the production Block.
template <typename InputSample>
struct BlockBenchmarkCase {
    std::string name;
    cy::flowgraph::ValueMap params;
    std::size_t N = 0;
    std::size_t M = 0;
    std::optional<double> target_input_mitems_per_second;
    std::optional<std::vector<InputSample>> input_frame;
};

struct BlockBenchmarkOptions {
    std::size_t warmup_works = 200;
    std::size_t minimum_measured_works = 1000;
    double minimum_measurement_seconds = 1.0;
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

inline std::size_t ParsePositiveBenchmarkSize(const char* text, const char* name) {
    const auto parsed = std::stoull(text);
    if (parsed == 0 || parsed > std::numeric_limits<std::size_t>::max()) {
        throw std::invalid_argument(std::string(name) + " must be positive");
    }
    return static_cast<std::size_t>(parsed);
}

inline std::size_t CheckedDoubleCapacity(std::size_t elements, const char* name) {
    if (elements == 0) {
        throw std::invalid_argument(std::string(name) + " must be positive");
    }
    if (elements > std::numeric_limits<std::size_t>::max() / 2) {
        throw std::overflow_error(std::string(name) + " ring capacity overflows");
    }
    return elements * 2;
}

inline double BenchmarkPercentile(const std::vector<double>& sorted_values,
                                  double percentile) {
    if (sorted_values.empty()) {
        return 0.0;
    }
    const double scaled =
        (percentile / 100.0) * static_cast<double>(sorted_values.size() - 1);
    return sorted_values[static_cast<std::size_t>(scaled)];
}

template <typename InputSample>
std::vector<InputSample> MakeDefaultBenchmarkInputFrame(std::size_t N) {
    static_assert(std::is_default_constructible<InputSample>::value,
                  "Benchmark InputSample must be default constructible");
    static_assert(std::is_trivially_copyable<InputSample>::value,
                  "Benchmark InputSample must be trivially copyable");
    return std::vector<InputSample>(N, InputSample{});
}

template <typename InputSample>
void ValidateBenchmarkCase(const BlockBenchmarkCase<InputSample>& benchmark_case,
                           const BlockBenchmarkOptions& options) {
    if (benchmark_case.name.empty()) {
        throw std::invalid_argument("benchmark case name must not be empty");
    }
    if (benchmark_case.N == 0 || benchmark_case.M == 0) {
        throw std::invalid_argument(
            "benchmark requires positive fixed input and output transaction sizes");
    }
    (void)CheckedDoubleCapacity(benchmark_case.N, "input elements per work");
    (void)CheckedDoubleCapacity(benchmark_case.M, "output elements per work");
    if (options.warmup_works == 0 || options.minimum_measured_works == 0 ||
        options.minimum_measurement_seconds <= 0.0) {
        throw std::invalid_argument(
            "warmup count, minimum measured count, and measurement duration must be positive");
    }
    if (benchmark_case.target_input_mitems_per_second &&
        *benchmark_case.target_input_mitems_per_second <= 0.0) {
        throw std::invalid_argument("target input rate must be positive");
    }
    if (benchmark_case.input_frame &&
        benchmark_case.input_frame->size() != benchmark_case.N) {
        throw std::invalid_argument(
            "pre-generated input frame size does not match the configured input count");
    }
}

template <typename Algorithm, typename InputSample, typename OutputSample>
std::unique_ptr<cy::flowgraph::BlockModel> MakeBenchmarkProductionBlock(
    const BlockBenchmarkCase<InputSample>& benchmark_case,
    const char* instance_name,
    const char* block_type_name) {
    using ProductionBlock =
        cycore::sdk::AlgorithmBlockAdapter<Algorithm, InputSample, OutputSample>;
    return std::unique_ptr<cy::flowgraph::BlockModel>(
        new cy::flowgraph::BlockWrapper<ProductionBlock>(
            instance_name, cy::flowgraph::BlockTypeName{block_type_name},
            benchmark_case.params));
}

template <typename InputSample, typename OutputSample>
WorkObservation RunPreparedBenchmarkWork(
    BlockTestHarness<InputSample, OutputSample>& harness,
    const BlockBenchmarkCase<InputSample>& benchmark_case,
    const std::vector<InputSample>& input_frame) {
    if (harness.output_available() != 0) {
        throw std::runtime_error("previous output transaction was not drained");
    }
    if (harness.output_writable() < benchmark_case.M) {
        throw std::runtime_error("output ring is backpressured before work()");
    }

    harness.publish(input_frame);
    if (harness.input_available() < benchmark_case.N) {
        throw std::runtime_error("input ring is starved before work()");
    }

    // BlockTestHarness times only the production BlockModel::work() call.
    const auto observation = harness.work_once();
    if (!observation.succeeded) {
        throw std::runtime_error("work() failed despite prepared input/output rings");
    }
    if (observation.consumed_input_elements != benchmark_case.N ||
        observation.produced_output_elements != benchmark_case.M) {
        throw std::runtime_error("work() violated the configured fixed N-to-M contract");
    }

    (void)harness.drain_one_transaction();
    return observation;
}

template <typename Algorithm, typename InputSample, typename OutputSample>
BlockBenchmarkResult RunBlockBenchmarkCase(
    const BlockBenchmarkCase<InputSample>& benchmark_case,
    const BlockBenchmarkOptions& options,
    const char* instance_name,
    const char* block_type_name) {
    BlockBenchmarkResult result;

    try {
        ValidateBenchmarkCase(benchmark_case, options);
        const std::size_t input_capacity =
            CheckedDoubleCapacity(benchmark_case.N, "input elements per work");
        const std::size_t output_capacity =
            CheckedDoubleCapacity(benchmark_case.M, "output elements per work");
        const auto default_input = benchmark_case.input_frame
                                       ? std::vector<InputSample>{}
                                       : MakeDefaultBenchmarkInputFrame<InputSample>(benchmark_case.N);
        const auto& input_frame = benchmark_case.input_frame
                                      ? *benchmark_case.input_frame
                                      : default_input;

        BlockTestHarness<InputSample, OutputSample> harness(
            MakeBenchmarkProductionBlock<Algorithm, InputSample, OutputSample>(
                benchmark_case, instance_name, block_type_name),
            benchmark_case.N,
            benchmark_case.M,
            input_capacity,
            output_capacity);

        for (std::size_t i = 0; i < options.warmup_works; ++i) {
            (void)RunPreparedBenchmarkWork(harness, benchmark_case, input_frame);
        }

        result.latency_us.reserve(options.minimum_measured_works);
        while (result.successful_works < options.minimum_measured_works ||
               result.measured_work_seconds < options.minimum_measurement_seconds) {
            try {
                const auto observation =
                    RunPreparedBenchmarkWork(harness, benchmark_case, input_frame);
                ++result.successful_works;
                result.input_elements += observation.consumed_input_elements;
                result.output_elements += observation.produced_output_elements;
                result.measured_work_seconds +=
                    std::chrono::duration<double>(observation.latency).count();
                result.latency_us.push_back(
                    std::chrono::duration<double, std::micro>(observation.latency).count());
            } catch (...) {
                ++result.failed_works;
                throw;
            }
        }

        if (result.successful_works < options.minimum_measured_works ||
            result.measured_work_seconds < options.minimum_measurement_seconds ||
            result.failed_works != 0 || result.measured_work_seconds <= 0.0) {
            throw std::runtime_error("benchmark did not complete every measured work");
        }
        result.passed = true;
    } catch (const std::exception& ex) {
        result.error = ex.what();
    }

    return result;
}

template <typename InputSample, typename OutputSample>
void PrintBlockBenchmarkResult(const BlockBenchmarkCase<InputSample>& benchmark_case,
                               const BlockBenchmarkOptions& options,
                               const BlockBenchmarkResult& result) {
    const bool invalid_capacity =
        benchmark_case.N == 0 || benchmark_case.M == 0 ||
        benchmark_case.N > std::numeric_limits<std::size_t>::max() / 2 ||
        benchmark_case.M > std::numeric_limits<std::size_t>::max() / 2;
    if (invalid_capacity) {
        std::cout << "\nComplete Block saturation limit: " << benchmark_case.name << '\n'
                  << "  status=FAILED"
                  << " successful_works=" << result.successful_works
                  << " failed_works=" << result.failed_works
                  << " error=\"" << result.error << "\"\n";
        return;
    }
    const std::size_t input_capacity =
        CheckedDoubleCapacity(benchmark_case.N, "input elements per work");
    const std::size_t output_capacity =
        CheckedDoubleCapacity(benchmark_case.M, "output elements per work");

    std::cout << "\nComplete Block saturation limit: " << benchmark_case.name << '\n'
              << "  scope=BlockModel::work only; excludes device/network/disk/scheduler\n"
              << "  input_type_key=" << cy::flowgraph::detail::type_key<InputSample>()
              << " input_item_size=" << sizeof(InputSample)
              << " output_type_key=" << cy::flowgraph::detail::type_key<OutputSample>()
              << " output_item_size=" << sizeof(OutputSample) << '\n'
              << "  input_elements_per_work=" << benchmark_case.N
              << " output_elements_per_work=" << benchmark_case.M
              << " input_bytes_per_work=" << benchmark_case.N * sizeof(InputSample)
              << " output_bytes_per_work=" << benchmark_case.M * sizeof(OutputSample) << '\n'
              << "  input_capacity=" << input_capacity
              << " output_capacity=" << output_capacity
              << " warmup_works=" << options.warmup_works
              << " minimum_measured_works=" << options.minimum_measured_works
              << " minimum_measurement_seconds=" << options.minimum_measurement_seconds
              << '\n';

    if (!result.passed) {
        std::cout << "  status=FAILED"
                  << " successful_works=" << result.successful_works
                  << " failed_works=" << result.failed_works
                  << " error=\"" << result.error << "\"\n";
        return;
    }

    auto latency_us = result.latency_us;
    std::sort(latency_us.begin(), latency_us.end());
    const double average_us =
        std::accumulate(latency_us.begin(), latency_us.end(), 0.0) /
        static_cast<double>(latency_us.size());
    const double input_mitems_per_second =
        static_cast<double>(result.input_elements) / result.measured_work_seconds / 1.0e6;
    const double output_mitems_per_second =
        static_cast<double>(result.output_elements) / result.measured_work_seconds / 1.0e6;
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
              << " p50=" << BenchmarkPercentile(latency_us, 50.0)
              << " p95=" << BenchmarkPercentile(latency_us, 95.0)
              << " p99=" << BenchmarkPercentile(latency_us, 99.0)
              << " max=" << latency_us.back() << '\n';

    if (benchmark_case.target_input_mitems_per_second) {
        std::cout << "  target_input_Mitems/s="
                  << *benchmark_case.target_input_mitems_per_second
                  << " realtime_headroom="
                  << input_mitems_per_second /
                         *benchmark_case.target_input_mitems_per_second
                  << "x\n";
    }
}

template <typename Algorithm, typename InputSample, typename OutputSample>
int RunBlockBenchmarks(int argc,
                       char** argv,
                       const char* instance_name,
                       const char* block_type_name,
                       const std::vector<BlockBenchmarkCase<InputSample>>& benchmark_cases) {
    try {
        const std::size_t measurement_milliseconds =
            argc > 1 ? ParsePositiveBenchmarkSize(argv[1], "measurement milliseconds") : 1000;
        const std::size_t warmup_works =
            argc > 2 ? ParsePositiveBenchmarkSize(argv[2], "warmup works") : 200;
        const std::size_t minimum_measured_works =
            argc > 3 ? ParsePositiveBenchmarkSize(argv[3], "minimum measured works") : 1000;
        const std::string case_filter = argc > 4 ? argv[4] : "";
        if (benchmark_cases.empty()) {
            throw std::invalid_argument("benchmark must define at least one case");
        }

        const BlockBenchmarkOptions options{
            warmup_works,
            minimum_measured_works,
            static_cast<double>(measurement_milliseconds) / 1000.0};
        bool matched = false;
        bool passed = true;
        for (const auto& benchmark_case : benchmark_cases) {
            if (!case_filter.empty() &&
                benchmark_case.name.find(case_filter) == std::string::npos) {
                continue;
            }
            matched = true;
            const auto result = RunBlockBenchmarkCase<Algorithm, InputSample, OutputSample>(
                benchmark_case, options, instance_name, block_type_name);
            PrintBlockBenchmarkResult<InputSample, OutputSample>(
                benchmark_case, options, result);
            passed = passed && result.passed;
        }
        if (!matched) {
            throw std::invalid_argument("benchmark case filter did not match any case");
        }
        return passed ? 0 : 1;
    } catch (const std::exception& ex) {
        std::cerr << "Block benchmark failed: " << ex.what() << '\n';
        return 1;
    }
}

} // namespace cycore::sdk::test
