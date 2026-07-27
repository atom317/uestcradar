#pragma once

#include <cycore_algorithm_sdk.h>

#include <flowgraph/port.h>
#include <flowgraph/value.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#if defined(_MSC_VER)
#include <malloc.h>
#endif

namespace cycore::benchmark {

struct BenchmarkOptions {
    std::chrono::milliseconds minimum_duration{1000};
    std::size_t warmup_frames = 100;
    std::size_t minimum_frames = 1000;
    std::size_t maximum_idle_rounds = 1024;
};

struct BenchmarkResult {
    std::string name;
    std::size_t frames = 0;
    std::size_t input_payload_bytes = 0;
    std::size_t input_wire_bytes = 0;
    std::size_t output_wire_bytes = 0;
    std::size_t framework_allocations = 0;
    double seconds = 0.0;
    double frames_per_second = 0.0;
    double payload_gib_per_second = 0.0;
    double average_latency_us = 0.0;
    std::uint64_t checksum = 0;
};

namespace detail {

inline std::atomic<bool> allocation_tracking_enabled{false};
inline std::atomic<std::size_t> allocation_count{0};

inline void record_allocation() noexcept {
    if (allocation_tracking_enabled.load(std::memory_order_relaxed)) {
        allocation_count.fetch_add(1, std::memory_order_relaxed);
    }
}

inline void* allocate(std::size_t size) {
    record_allocation();
    if (void* pointer = std::malloc(size == 0 ? 1 : size)) {
        return pointer;
    }
    throw std::bad_alloc();
}

inline void* allocate_aligned(std::size_t size, std::size_t alignment) {
    record_allocation();
    void* pointer = nullptr;
#if defined(_MSC_VER)
    pointer = _aligned_malloc(size == 0 ? 1 : size, alignment);
    if (pointer == nullptr) {
        throw std::bad_alloc();
    }
#else
    if (posix_memalign(&pointer, alignment, size == 0 ? 1 : size) != 0) {
        throw std::bad_alloc();
    }
#endif
    return pointer;
}

inline void deallocate_aligned(void* pointer) noexcept {
#if defined(_MSC_VER)
    _aligned_free(pointer);
#else
    std::free(pointer);
#endif
}

inline void begin_allocation_tracking() noexcept {
    allocation_count.store(0, std::memory_order_relaxed);
    allocation_tracking_enabled.store(true, std::memory_order_release);
}

inline std::size_t end_allocation_tracking() noexcept {
    allocation_tracking_enabled.store(false, std::memory_order_release);
    return allocation_count.load(std::memory_order_relaxed);
}

inline std::size_t parse_size(const char* text,
                              std::size_t fallback,
                              const char* name) {
    if (text == nullptr) {
        return fallback;
    }
    try {
        std::size_t consumed = 0;
        const auto value = std::stoull(text, &consumed);
        if (consumed != std::strlen(text) ||
            value > std::numeric_limits<std::size_t>::max()) {
            throw std::out_of_range(name);
        }
        return static_cast<std::size_t>(value);
    } catch (const std::exception&) {
        throw std::invalid_argument(std::string("invalid ") + name + ": " + text);
    }
}

template <typename Algorithm, typename = void>
struct BenchmarkParams {
    static cy::flowgraph::ValueMap get() {
        return {};
    }
};

template <typename Algorithm>
struct BenchmarkParams<
    Algorithm,
    std::void_t<decltype(Algorithm::benchmark_params())>> {
    static cy::flowgraph::ValueMap get() {
        return Algorithm::benchmark_params();
    }
};

inline void publish(cy::flowgraph::PortOut<std::byte>& source,
                    cy::common::Span<const std::byte> wire) {
    std::size_t offset = 0;
    while (offset < wire.size()) {
        auto span = source.reserve(wire.size() - offset);
        if (span.empty()) {
            throw std::runtime_error(
                "benchmark source backpressured before publishing one frame");
        }
        std::memcpy(span.data(), wire.data() + offset, span.size());
        offset += span.size();
        span.commit(span.size());
    }
}

inline void update_checksum(std::uint64_t& checksum,
                            cy::common::Span<const std::byte> payload) noexcept {
    if (payload.empty()) {
        return;
    }
    const std::size_t middle = payload.size() / 2;
    checksum = checksum * 1099511628211ULL +
               std::to_integer<std::uint8_t>(payload[0]);
    checksum = checksum * 1099511628211ULL +
               std::to_integer<std::uint8_t>(payload[middle]);
    checksum = checksum * 1099511628211ULL +
               std::to_integer<std::uint8_t>(payload[payload.size() - 1]);
}

template <typename Algorithm>
class FrameBenchmarkDriver {
public:
    using InputData = typename Algorithm::InputData;
    using OutputData = typename Algorithm::OutputData;
    using InputCodec = cycore::sdk::TrivialFrameCodec<InputData>;
    using OutputCodec = cycore::sdk::TrivialFrameCodec<OutputData>;

    template <typename Prepare>
    explicit FrameBenchmarkDriver(Prepare&& prepare)
        : values_(BenchmarkParams<Algorithm>::get()),
          input_data_(std::make_unique<InputData>()),
          output_data_(std::make_unique<OutputData>()) {
        static_assert(std::is_trivially_copyable_v<InputData>,
                      "Non-trivial data requires a custom FrameCodec, POD only");
        static_assert(std::is_trivially_copyable_v<OutputData>,
                      "Non-trivial data requires a custom FrameCodec, POD only");
        static_assert(std::is_default_constructible_v<InputData>,
                      "Benchmark InputData must be default constructible");
        static_assert(std::is_default_constructible_v<OutputData>,
                      "Benchmark OutputData must be default constructible");

        prepare(*input_data_);
        constexpr std::size_t input_payload_bytes = sizeof(InputData);
        input_payload_.resize(input_payload_bytes);
        if (!InputCodec::encode(
                *input_data_,
                cy::common::Span<std::byte>(
                    input_payload_.data(), input_payload_.size()))) {
            throw std::runtime_error("benchmark input POD copy failed");
        }

        constexpr std::size_t output_payload_bytes = sizeof(OutputData);
        input_wire_bytes_ =
            cycore::sdk::WireFrameBytes(input_payload_.size());
        output_wire_bytes_ =
            cycore::sdk::WireFrameBytes(output_payload_bytes);

        input_wire_.resize(input_wire_bytes_);
        output_wire_.resize(output_wire_bytes_);
        adapter_ = std::make_unique<
            cycore::sdk::FrameAlgorithmAdapter<Algorithm>>(values_);
        const std::size_t capacity =
            std::max(input_wire_bytes_, output_wire_bytes_);
        cy::flowgraph::connect(source_, adapter_->in, capacity);
        cy::flowgraph::connect(adapter_->out, sink_, capacity);
    }

    void run_one(std::uint64_t sequence_id) {
        std::size_t encoded_wire_bytes = 0;
        if (!cycore::sdk::EncodeFrame(
                cy::common::Span<const std::byte>(
                    input_payload_.data(), input_payload_.size()),
                cycore::sdk::FrameMetadata{sequence_id, sequence_id},
                cy::common::Span<std::byte>(
                    input_wire_.data(), input_wire_.size()),
                &encoded_wire_bytes) ||
            encoded_wire_bytes != input_wire_bytes_) {
            throw std::runtime_error("benchmark SDK input frame encode failed");
        }
        publish(
            source_,
            cy::common::Span<const std::byte>(
                input_wire_.data(), input_wire_.size()));

        const std::uint64_t emitted_before =
            adapter_->stats().frames_emitted;
        std::size_t idle_rounds = 0;
        while (adapter_->stats().frames_emitted == emitted_before) {
            if (adapter_->work()) {
                idle_rounds = 0;
            } else if (++idle_rounds > maximum_idle_rounds_) {
                throw std::runtime_error(
                    "benchmark Adapter stalled before emitting one frame");
            }
        }
        if (adapter_->stats().frames_emitted != emitted_before + 1) {
            throw std::runtime_error(
                "benchmark Adapter emitted an unexpected frame count");
        }

        if (sink_.available() < cycore::sdk::kFrameEnvelopeBytes) {
            throw std::runtime_error(
                "benchmark sink received an incomplete frame envelope");
        }
        auto header = cy::common::Span<std::byte>(
            output_wire_.data(), cycore::sdk::kFrameEnvelopeBytes);
        if (sink_.peek_copy(0, header) != header.size()) {
            throw std::runtime_error("benchmark sink header peek failed");
        }
        auto inspection = cycore::sdk::InspectFrame(
            cy::common::Span<const std::byte>(header.data(), header.size()),
            output_wire_bytes_);
        if (inspection.wire_bytes == 0 ||
            inspection.wire_bytes > output_wire_.size() ||
            sink_.available() < inspection.wire_bytes) {
            throw std::runtime_error(
                "benchmark sink did not receive one complete output frame");
        }

        auto wire = cy::common::Span<std::byte>(
            output_wire_.data(), inspection.wire_bytes);
        if (sink_.peek_copy(0, wire) != wire.size()) {
            throw std::runtime_error("benchmark sink frame peek failed");
        }
        inspection = cycore::sdk::InspectFrame(
            cy::common::Span<const std::byte>(wire.data(), wire.size()),
            output_wire_bytes_);
        if (inspection.status !=
            cycore::sdk::FrameParseStatus::CompleteFrame) {
            throw std::runtime_error(
                "benchmark sink rejected the output frame");
        }
        if (inspection.metadata.sequence_id != sequence_id) {
            throw std::runtime_error(
                "benchmark output sequence_id was not propagated");
        }

        const auto output_payload = cy::common::Span<const std::byte>(
            output_wire_.data() + cycore::sdk::kFrameEnvelopeBytes,
            inspection.payload_bytes);
        if (!OutputCodec::decode(output_payload, *output_data_)) {
            throw std::runtime_error(
                "benchmark output POD copy failed");
        }
        update_checksum(checksum_, output_payload);
        if (!sink_.consume_exact(inspection.wire_bytes)) {
            throw std::runtime_error(
                "benchmark sink failed to consume the output frame");
        }
    }

    void set_maximum_idle_rounds(std::size_t value) noexcept {
        maximum_idle_rounds_ = value;
    }

    std::size_t input_payload_bytes() const noexcept {
        return input_payload_.size();
    }

    std::size_t input_wire_bytes() const noexcept {
        return input_wire_bytes_;
    }

    std::size_t output_wire_bytes() const noexcept {
        return output_wire_bytes_;
    }

    std::uint64_t checksum() const noexcept {
        return checksum_;
    }

private:
    cy::flowgraph::ValueMap values_;
    std::unique_ptr<InputData> input_data_;
    std::unique_ptr<OutputData> output_data_;
    std::vector<std::byte> input_payload_;
    std::vector<std::byte> input_wire_;
    std::vector<std::byte> output_wire_;
    cy::flowgraph::PortOut<std::byte> source_;
    std::unique_ptr<cycore::sdk::FrameAlgorithmAdapter<Algorithm>> adapter_;
    cy::flowgraph::PortIn<std::byte> sink_;
    std::size_t input_wire_bytes_ = 0;
    std::size_t output_wire_bytes_ = 0;
    std::size_t maximum_idle_rounds_ = 1024;
    std::uint64_t checksum_ = 1469598103934665603ULL;
};

} // namespace detail

template <typename Algorithm, typename Prepare>
BenchmarkResult RunFrameBenchmark(std::string_view name,
                                  Prepare&& prepare,
                                  BenchmarkOptions options = {}) {
    if (options.minimum_frames == 0) {
        throw std::invalid_argument(
            "benchmark minimum_frames must be positive");
    }
    if (options.maximum_idle_rounds == 0) {
        throw std::invalid_argument(
            "benchmark maximum_idle_rounds must be positive");
    }

    detail::FrameBenchmarkDriver<Algorithm> driver{
        std::forward<Prepare>(prepare)};
    driver.set_maximum_idle_rounds(options.maximum_idle_rounds);

    std::uint64_t sequence_id = 1;
    for (std::size_t frame = 0; frame < options.warmup_frames; ++frame) {
        driver.run_one(sequence_id++);
    }

    detail::begin_allocation_tracking();
    const auto start = std::chrono::steady_clock::now();
    std::size_t frames = 0;
    do {
        driver.run_one(sequence_id++);
        ++frames;
    } while (frames < options.minimum_frames ||
             std::chrono::steady_clock::now() - start <
                 options.minimum_duration);
    const auto end = std::chrono::steady_clock::now();
    const std::size_t allocations = detail::end_allocation_tracking();

    BenchmarkResult result;
    result.name = std::string(name);
    result.frames = frames;
    result.input_payload_bytes = driver.input_payload_bytes();
    result.input_wire_bytes = driver.input_wire_bytes();
    result.output_wire_bytes = driver.output_wire_bytes();
    result.framework_allocations = allocations;
    result.seconds =
        std::chrono::duration<double>(end - start).count();
    result.frames_per_second =
        static_cast<double>(frames) / result.seconds;
    result.payload_gib_per_second =
        result.frames_per_second *
        static_cast<double>(result.input_payload_bytes) /
        static_cast<double>(std::uint64_t{1} << 30U);
    result.average_latency_us =
        result.seconds * 1.0e6 / static_cast<double>(frames);
    result.checksum = driver.checksum();
    return result;
}

inline void PrintBenchmarkResult(const BenchmarkResult& result,
                                 std::ostream& output = std::cout) {
    output << "benchmark=" << result.name
           << " frames=" << result.frames
           << " input_payload_bytes=" << result.input_payload_bytes
           << " input_wire_bytes=" << result.input_wire_bytes
           << " output_wire_bytes=" << result.output_wire_bytes
           << " frames/s=" << result.frames_per_second
           << " payload_gib_per_s=" << result.payload_gib_per_second
           << " average_latency_us=" << result.average_latency_us
           << " framework_allocations=" << result.framework_allocations
           << " checksum=" << result.checksum << '\n';
}

template <typename Algorithm, typename Prepare>
int RunRegisteredBenchmark(int argc,
                           char** argv,
                           std::string_view name,
                           Prepare&& prepare) {
    try {
        BenchmarkOptions options;
        if (argc > 1) {
            options.minimum_duration = std::chrono::milliseconds(
                detail::parse_size(
                    argv[1],
                    static_cast<std::size_t>(
                        options.minimum_duration.count()),
                    "minimum_duration_ms"));
        }
        if (argc > 2) {
            options.warmup_frames = detail::parse_size(
                argv[2], options.warmup_frames, "warmup_frames");
        }
        if (argc > 3) {
            options.minimum_frames = detail::parse_size(
                argv[3], options.minimum_frames, "minimum_frames");
        }
        if (argc > 4) {
            throw std::invalid_argument(
                "usage: benchmark [minimum_duration_ms] "
                "[warmup_frames] [minimum_frames]");
        }

        const auto result = RunFrameBenchmark<Algorithm>(
            name, std::forward<Prepare>(prepare), options);
        PrintBenchmarkResult(result);
        return result.framework_allocations == 0 ? 0 : 2;
    } catch (const std::exception& error) {
        std::cerr << "benchmark failed: " << error.what() << '\n';
        return 1;
    }
}

} // namespace cycore::benchmark

#define CYCORE_BENCHMARK_DEFINE_ALLOCATION_OPERATORS()                         \
    void* operator new(std::size_t size) {                                     \
        return ::cycore::benchmark::detail::allocate(size);                    \
    }                                                                          \
    void* operator new[](std::size_t size) {                                   \
        return ::cycore::benchmark::detail::allocate(size);                    \
    }                                                                          \
    void* operator new(std::size_t size, std::align_val_t alignment) {         \
        return ::cycore::benchmark::detail::allocate_aligned(                  \
            size, static_cast<std::size_t>(alignment));                        \
    }                                                                          \
    void* operator new[](std::size_t size, std::align_val_t alignment) {       \
        return ::cycore::benchmark::detail::allocate_aligned(                  \
            size, static_cast<std::size_t>(alignment));                        \
    }                                                                          \
    void operator delete(void* pointer) noexcept {                             \
        std::free(pointer);                                                     \
    }                                                                          \
    void operator delete[](void* pointer) noexcept {                           \
        std::free(pointer);                                                     \
    }                                                                          \
    void operator delete(void* pointer, std::size_t) noexcept {                \
        std::free(pointer);                                                     \
    }                                                                          \
    void operator delete[](void* pointer, std::size_t) noexcept {              \
        std::free(pointer);                                                     \
    }                                                                          \
    void operator delete(void* pointer, std::align_val_t) noexcept {           \
        ::cycore::benchmark::detail::deallocate_aligned(pointer);              \
    }                                                                          \
    void operator delete[](void* pointer, std::align_val_t) noexcept {         \
        ::cycore::benchmark::detail::deallocate_aligned(pointer);              \
    }                                                                          \
    void operator delete(                                                       \
        void* pointer, std::size_t, std::align_val_t) noexcept {               \
        ::cycore::benchmark::detail::deallocate_aligned(pointer);              \
    }                                                                          \
    void operator delete[](                                                     \
        void* pointer, std::size_t, std::align_val_t) noexcept {               \
        ::cycore::benchmark::detail::deallocate_aligned(pointer);              \
    }

#define CYCORE_REGISTER_BENCHMARK(algorithm_type, ...)                         \
    CYCORE_BENCHMARK_DEFINE_ALLOCATION_OPERATORS()                             \
    int main(int argc, char** argv) {                                           \
        return ::cycore::benchmark::RunRegisteredBenchmark<algorithm_type>(    \
            argc, argv, #algorithm_type, (__VA_ARGS__));                       \
    }
