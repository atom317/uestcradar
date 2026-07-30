#include <data.h>
#include <sdk.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <time.h>

namespace {

constexpr std::uint64_t kPhaseShift = 62;
constexpr std::uint64_t kSequenceMask =
    (std::uint64_t{1} << kPhaseShift) - 1;
constexpr std::uint64_t kWarmup = 0;
constexpr std::uint64_t kMeasure = 1;
constexpr std::uint64_t kEnd = 2;
constexpr std::size_t kHistogramBuckets = 1'000'002;

struct Arguments {
    std::string mode;
    std::size_t payload_bytes{64 * 1024};
    double warmup_seconds{2.0};
    double duration_seconds{10.0};
    double rate_mib_s{0.0};
    double wave_period_seconds{4.0};
};

std::uint64_t clock_ns(clockid_t clock) {
    struct timespec value {};
    if (::clock_gettime(clock, &value) != 0) {
        throw std::runtime_error("clock_gettime failed");
    }
    return static_cast<std::uint64_t>(value.tv_sec) *
               1'000'000'000ULL +
           static_cast<std::uint64_t>(value.tv_nsec);
}

std::uint64_t monotonic_ns() {
    return clock_ns(CLOCK_MONOTONIC_RAW);
}

std::uint64_t unix_ns() {
    return clock_ns(CLOCK_REALTIME);
}

double process_cpu_seconds() {
    return static_cast<double>(
               clock_ns(CLOCK_PROCESS_CPUTIME_ID)) /
           1e9;
}

double parse_double(const char* value, const char* option) {
    char* end = nullptr;
    errno = 0;
    const double result = std::strtod(value, &end);
    if (errno != 0 || end == value || *end != '\0' ||
        !std::isfinite(result) || result < 0.0) {
        throw std::invalid_argument(std::string{"invalid "} + option);
    }
    return result;
}

std::size_t parse_size(const char* value, const char* option) {
    char* end = nullptr;
    errno = 0;
    const unsigned long long result =
        std::strtoull(value, &end, 10);
    constexpr std::size_t minimum =
        sizeof(uestcradar::IQMetadata) +
        sizeof(uestcradar::ComplexInt16);
    if (errno != 0 || end == value || *end != '\0' ||
        result < minimum || result > INT32_MAX) {
        throw std::invalid_argument(std::string{"invalid "} + option);
    }
    return static_cast<std::size_t>(result);
}

Arguments parse_arguments(int argc, char* argv[]) {
    Arguments result;
    for (int index = 1; index < argc; ++index) {
        const std::string_view option{argv[index]};
        auto next = [&]() -> const char* {
            if (++index >= argc) {
                throw std::invalid_argument(
                    std::string{"missing value for "} +
                    std::string{option});
            }
            return argv[index];
        };
        if (option == "--mode") {
            result.mode = next();
        } else if (option == "--payload-bytes") {
            result.payload_bytes =
                parse_size(next(), "--payload-bytes");
        } else if (option == "--warmup-seconds") {
            result.warmup_seconds =
                parse_double(next(), "--warmup-seconds");
        } else if (option == "--duration-seconds") {
            result.duration_seconds =
                parse_double(next(), "--duration-seconds");
        } else if (option == "--rate-mib-s") {
            result.rate_mib_s =
                parse_double(next(), "--rate-mib-s");
        } else if (option == "--wave-period-seconds") {
            result.wave_period_seconds =
                parse_double(next(), "--wave-period-seconds");
        } else {
            throw std::invalid_argument(
                std::string{"unknown option: "} +
                std::string{option});
        }
    }
    if ((result.mode != "producer" &&
         result.mode != "consumer") ||
        result.duration_seconds <= 0.0 ||
        (result.rate_mib_s > 0.0 &&
         result.wave_period_seconds <= 0.0)) {
        throw std::invalid_argument(
            "--mode must be producer or consumer and durations positive");
    }
    return result;
}

std::uint64_t encode_frame_id(
    std::uint64_t sequence,
    std::uint64_t phase) {
    return (phase << kPhaseShift) |
           (sequence & kSequenceMask);
}

std::uint64_t phase_from(std::uint64_t frame_id) {
    return frame_id >> kPhaseShift;
}

std::size_t samples_for_payload(std::size_t requested_bytes) {
    return (requested_bytes - sizeof(uestcradar::IQMetadata)) /
           sizeof(uestcradar::ComplexInt16);
}

std::size_t actual_payload_bytes(std::size_t samples) {
    return sizeof(uestcradar::IQMetadata) +
           samples * sizeof(uestcradar::ComplexInt16);
}

void write_record(
    uestcradar::Output<uestcradar::IQFrame>& output,
    std::size_t samples,
    std::uint64_t sequence,
    std::uint64_t phase) {
    auto frame = output.create({
        .frame_id = encode_frame_id(sequence, phase),
        .timestamp_unix_ns = unix_ns(),
        .channel_count = 1,
        .samples_per_channel =
            static_cast<std::uint32_t>(samples),
        .sample_rate_hz = 1.0,
        .center_frequency_hz = 0.0,
    });
    std::fill(
        frame.data.values().begin(),
        frame.data.values().end(),
        uestcradar::ComplexInt16{});
    output.write(frame);
}

void pace(
    const Arguments& arguments,
    std::uint64_t bytes_sent,
    std::uint64_t started_ns) {
    if (arguments.rate_mib_s == 0.0) {
        return;
    }
    const double elapsed =
        static_cast<double>(monotonic_ns() - started_ns) / 1e9;
    constexpr double pi = 3.14159265358979323846;
    const double wave =
        0.625 +
        0.375 *
            std::sin(
                2.0 * pi * elapsed /
                arguments.wave_period_seconds);
    const double current_mib_s =
        arguments.rate_mib_s * std::max(0.25, wave);
    const double target_seconds =
        static_cast<double>(bytes_sent) /
        (current_mib_s * 1024.0 * 1024.0);
    const std::uint64_t target_ns =
        started_ns +
        static_cast<std::uint64_t>(target_seconds * 1e9);
    const std::uint64_t now = monotonic_ns();
    if (target_ns > now) {
        std::this_thread::sleep_for(
            std::chrono::nanoseconds{target_ns - now});
    }
}

void run_producer(const Arguments& arguments) {
    uestcradar::Output<uestcradar::IQFrame> output;
    const std::size_t samples =
        samples_for_payload(arguments.payload_bytes);
    const std::size_t payload_bytes =
        actual_payload_bytes(samples);
    std::uint64_t sequence = 0;

    const std::uint64_t warmup_ends =
        monotonic_ns() +
        static_cast<std::uint64_t>(
            arguments.warmup_seconds * 1e9);
    while (monotonic_ns() < warmup_ends) {
        write_record(output, samples, ++sequence, kWarmup);
    }

    const double cpu_started = process_cpu_seconds();
    const std::uint64_t started = monotonic_ns();
    const std::uint64_t ends =
        started +
        static_cast<std::uint64_t>(
            arguments.duration_seconds * 1e9);
    std::uint64_t messages = 0;
    while (monotonic_ns() < ends) {
        write_record(output, samples, ++sequence, kMeasure);
        ++messages;
        pace(
            arguments,
            messages * payload_bytes,
            started);
    }
    const std::uint64_t ended = monotonic_ns();
    const double cpu = process_cpu_seconds() - cpu_started;
    write_record(output, samples, ++sequence, kEnd);

    const double wall =
        static_cast<double>(ended - started) / 1e9;
    const double mib =
        static_cast<double>(messages * payload_bytes) /
        (1024.0 * 1024.0);
    std::cout << "{\"benchmark\":\"worker-producer\""
              << ",\"payload_bytes\":" << payload_bytes
              << ",\"messages\":" << messages
              << ",\"duration_s\":" << wall
              << ",\"payload_mib_s\":" << mib / wall
              << ",\"messages_s\":" << messages / wall
              << ",\"cpu_pct\":" << cpu / wall * 100.0 << "}\n";
}

std::uint64_t percentile(
    const std::vector<std::uint64_t>& histogram,
    std::uint64_t samples,
    double fraction) {
    const std::uint64_t target =
        static_cast<std::uint64_t>(
            std::ceil(static_cast<double>(samples) * fraction));
    std::uint64_t accumulated = 0;
    for (std::size_t bucket = 0;
         bucket < histogram.size();
         ++bucket) {
        accumulated += histogram[bucket];
        if (accumulated >= target) {
            return bucket;
        }
    }
    return histogram.size() - 1;
}

void run_consumer(const Arguments&) {
    uestcradar::Input<uestcradar::IQFrame> input;
    std::vector<std::uint64_t> latency_us(kHistogramBuckets);
    std::uint64_t messages = 0;
    std::uint64_t bytes = 0;
    std::uint64_t latency_total_ns = 0;
    std::uint64_t first_received_ns = 0;
    std::uint64_t last_received_ns = 0;
    double cpu_started = 0.0;

    for (;;) {
        auto frame = input.read();
        const std::uint64_t received_ns = unix_ns();
        const std::uint64_t phase =
            phase_from(frame.metadata.frame_id);
        if (phase == kEnd) {
            break;
        }
        if (phase != kMeasure) {
            continue;
        }
        if (messages == 0) {
            first_received_ns = received_ns;
            cpu_started = process_cpu_seconds();
        }
        last_received_ns = received_ns;
        const std::uint64_t latency_ns =
            received_ns >= frame.metadata.timestamp_unix_ns
                ? received_ns - frame.metadata.timestamp_unix_ns
                : 0;
        const std::size_t bucket = std::min<std::size_t>(
            latency_ns / 1000, latency_us.size() - 1);
        ++latency_us[bucket];
        latency_total_ns += latency_ns;
        ++messages;
        bytes += sizeof(uestcradar::IQMetadata) +
                 frame.data.values().size_bytes();
    }
    const double cpu = process_cpu_seconds() - cpu_started;
    if (messages == 0) {
        throw std::runtime_error("no measured messages received");
    }
    const double wall = std::max(
        1e-9,
        static_cast<double>(
            last_received_ns - first_received_ns) /
            1e9);
    const double mib =
        static_cast<double>(bytes) / (1024.0 * 1024.0);
    std::cout << "{\"benchmark\":\"worker-consumer\""
              << ",\"messages\":" << messages
              << ",\"duration_s\":" << wall
              << ",\"effective_mib_s\":" << mib / wall
              << ",\"messages_s\":" << messages / wall
              << ",\"latency_us_mean\":"
              << static_cast<double>(latency_total_ns) /
                     static_cast<double>(messages) / 1000.0
              << ",\"latency_us_p50\":"
              << percentile(latency_us, messages, 0.50)
              << ",\"latency_us_p99\":"
              << percentile(latency_us, messages, 0.99)
              << ",\"cpu_pct\":" << cpu / wall * 100.0 << "}\n";
}

}  // namespace

int main(int argc, char* argv[]) {
    try {
        const Arguments arguments = parse_arguments(argc, argv);
        if (arguments.mode == "producer") {
            run_producer(arguments);
        } else {
            run_consumer(arguments);
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "e2e-benchmark: " << error.what() << '\n';
        return 1;
    }
}
