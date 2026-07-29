#include "pulse_compression_algorithm.h"

#include "pulse_compression_fft_backend.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDefaultSampleRate = 30.72e6;
constexpr double kDefaultBandwidth = 20.0e6;
constexpr double kDefaultPulseDuration =
    static_cast<double>(pulse_compression_data::kReplicaPoints) /
    kDefaultSampleRate;
constexpr double kDefaultStartFrequency = -10.0e6;

std::int16_t ClampS16(float value) noexcept {
    if (value >= 32767.0F) {
        return 32767;
    }
    if (value <= -32768.0F) {
        return -32768;
    }
    return static_cast<std::int16_t>(
        value >= 0.0F ? value + 0.5F : value - 0.5F);
}

} // namespace

namespace pulse_compression_detail {

class PulseCompressionImplementation {
public:
    PulseCompressionImplementation(std::size_t points,
                                   std::size_t replica_points,
                                   double sample_rate,
                                   double bandwidth,
                                   double pulse_duration,
                                   double start_frequency)
        : points_(points),
          replica_points_(replica_points),
          fft_(points, replica_points, 1, 1) {
        if (replica_points_ == 0 || replica_points_ > points_) {
            throw std::invalid_argument(
                "replica_points must be in [1, points]");
        }
        if (!(sample_rate > 0.0) || !(bandwidth > 0.0) ||
            !(pulse_duration > 0.0) || bandwidth > sample_rate) {
            throw std::invalid_argument(
                "invalid pulse-compression waveform parameters");
        }

        std::vector<Complex32> impulse(replica_points_);
        const double chirp_slope = bandwidth / pulse_duration;
        for (std::size_t sample = 0; sample < replica_points_; ++sample) {
            const double time = static_cast<double>(sample) / sample_rate;
            const double phase =
                2.0 * kPi * start_frequency * time +
                kPi * chirp_slope * time * time;
            impulse[replica_points_ - 1 - sample] = {
                static_cast<float>(std::cos(phase)),
                static_cast<float>(-std::sin(phase)),
            };
        }
        fft_.build_matched_filter_spectrum(
            impulse.data(), impulse.size());
    }

    void process(const std::vector<cy::common::CS16>& input,
                 std::vector<cy::common::CS16>& output) noexcept {
        Complex32* const work = fft_.data();
        for (std::size_t sample = 0; sample < points_; ++sample) {
            work[sample] = {
                static_cast<float>(input[sample].i),
                static_cast<float>(input[sample].q),
            };
        }
        std::fill(
            work + points_,
            work + fft_.fft_size(),
            Complex32{0.0F, 0.0F});

        fft_.execute_forward();
        fft_.multiply_by_filter();
        fft_.execute_inverse();

        output.resize(points_);
        const float scale =
            1.0F /
            (static_cast<float>(fft_.fft_size()) *
             static_cast<float>(replica_points_));
        const Complex32* const aligned = work + replica_points_ - 1;
        for (std::size_t sample = 0; sample < points_; ++sample) {
            output[sample] = {
                ClampS16(aligned[sample].re * scale),
                ClampS16(aligned[sample].im * scale),
            };
        }
    }

private:
    std::size_t points_;
    std::size_t replica_points_;
    FftwBatchBackend fft_;
};

} // namespace pulse_compression_detail

PulseCompressionAlgorithm::PulseCompressionAlgorithm(
    const cycore::sdk::Params& params)
    : points_(ReadSizeParam(
          params,
          "points",
          pulse_compression_data::kDefaultPoints)),
      implementation_(
          std::make_unique<
              pulse_compression_detail::PulseCompressionImplementation>(
              points_,
              ReadSizeParam(
                  params,
                  "replica_points",
                  pulse_compression_data::kReplicaPoints),
              ReadPositiveParam(
                  params, "samp_rate", kDefaultSampleRate),
              ReadPositiveParam(
                  params, "bandwidth", kDefaultBandwidth),
              ReadPositiveParam(
                  params,
                  "pulse_duration",
                  kDefaultPulseDuration),
              params.get<double>(
                  "start_frequency", kDefaultStartFrequency))) {}

PulseCompressionAlgorithm::~PulseCompressionAlgorithm() = default;

cycore::sdk::ProcessResult PulseCompressionAlgorithm::work(
    const InputData& input,
    OutputData& output) noexcept {
    if (input.header.points == 0 ||
        input.header.points != points_ ||
        input.payload.size() != points_) {
        return cycore::sdk::ProcessResult::Drop;
    }

    output.header.points = input.header.points;
    implementation_->process(input.payload, output.payload);
    return cycore::sdk::ProcessResult::Produced;
}

cy::flowgraph::ValueMap PulseCompressionAlgorithm::benchmark_params() {
    return {
        {"points",
         static_cast<std::int64_t>(
             pulse_compression_data::kDefaultPoints)},
        {"replica_points",
         static_cast<std::int64_t>(
             pulse_compression_data::kReplicaPoints)},
        {"samp_rate", kDefaultSampleRate},
        {"bandwidth", kDefaultBandwidth},
        {"pulse_duration", kDefaultPulseDuration},
        {"start_frequency", kDefaultStartFrequency},
    };
}

std::size_t PulseCompressionAlgorithm::ReadSizeParam(
    const cycore::sdk::Params& params,
    const char* key,
    std::size_t fallback) {
    const auto value = params.get<std::int64_t>(
        key, static_cast<std::int64_t>(fallback));
    if (value <= 0) {
        throw std::invalid_argument(
            std::string(key) + " must be positive");
    }
    return static_cast<std::size_t>(value);
}

double PulseCompressionAlgorithm::ReadPositiveParam(
    const cycore::sdk::Params& params,
    const char* key,
    double fallback) {
    const double value = params.get<double>(key, fallback);
    if (!(value > 0.0) || !std::isfinite(value)) {
        throw std::invalid_argument(
            std::string(key) + " must be finite and positive");
    }
    return value;
}
