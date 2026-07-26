#include "pulse_compression_algorithm.h"

#include "pulse_compression_fft_backend.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kSampleRate = 30.72e6;
constexpr double kPulseWidth = 256.0 / kSampleRate;
constexpr double kBandwidth = 20e6;
constexpr double kChirpSlope = kBandwidth / kPulseWidth;
constexpr double kStartFreq = -10e6;

} // namespace

PulseCompressionAlgorithm::PulseCompressionAlgorithm(const cycore::sdk::Params& params)
    : num_channels_(ReadSizeParam(
          params, "num_channels", pulse_compression_data::kDefaultNumChannels)),
      num_pulses_(ReadSizeParam(
          params, "num_pulses", pulse_compression_data::kDefaultNumPulses)),
      samples_per_pulse_(ReadSizeParam(
          params, "samples_per_pulse", pulse_compression_data::kDefaultSamplesPerPulse)),
      fft_(std::make_unique<pulse_compression_detail::FftwBatchBackend>(
          samples_per_pulse_, num_channels_ * num_pulses_)) {
    std::array<pulse_compression_detail::Complex32, kReplicaLength> impulse{};
    for (std::size_t m = 0; m < kReplicaLength; ++m) {
        const double t = static_cast<double>(m) / kSampleRate;
        const double phase = 2.0 * kPi * kStartFreq * t + kPi * kChirpSlope * t * t;
        // Reverse-conjugated replica: convolution index s + 255 is the
        // historical forward-correlation result for output sample s.
        impulse[kReplicaLength - 1 - m] = {
            static_cast<float>(std::cos(phase)), static_cast<float>(-std::sin(phase))};
    }
    fft_->build_matched_filter_spectrum(impulse.data(), impulse.size());
}

PulseCompressionAlgorithm::~PulseCompressionAlgorithm() = default;

bool PulseCompressionAlgorithm::work(
    cycore::sdk::Reader<pulse_compression_data::InputSample>& in,
    cycore::sdk::Writer<pulse_compression_data::OutputSample>& out) {
    auto input = in.read_cube(num_channels_, num_pulses_, samples_per_pulse_);
    if (!input) return false;
    auto output = out.reserve_cube(num_channels_, num_pulses_, samples_per_pulse_);
    if (!output) return false;

    const std::size_t fft_size = fft_->fft_size();
    const std::size_t batch = fft_->batch_count();
    auto* work = fft_->data();
    std::fill_n(work, fft_size * batch, pulse_compression_detail::Complex32{0.0F, 0.0F});

    for (std::size_t pulse = 0; pulse < num_pulses_; ++pulse) {
        for (std::size_t channel = 0; channel < num_channels_; ++channel) {
            const std::size_t sequence = pulse * num_channels_ + channel;
            auto* destination = work + sequence * fft_size;
            for (std::size_t sample = 0; sample < samples_per_pulse_; ++sample) {
                const auto value = (*input)(channel, pulse, sample);
                destination[sample] = {static_cast<float>(value.i), static_cast<float>(value.q)};
            }
        }
    }

    fft_->execute_forward();
    fft_->multiply_by_filter();
    fft_->execute_inverse();

    const float scale = 1.0F /
                        (static_cast<float>(fft_size) * static_cast<float>(kReplicaLength));
    for (std::size_t pulse = 0; pulse < num_pulses_; ++pulse) {
        for (std::size_t channel = 0; channel < num_channels_; ++channel) {
            const std::size_t sequence = pulse * num_channels_ + channel;
            const auto* source = work + sequence * fft_size + (kReplicaLength - 1);
            for (std::size_t sample = 0; sample < samples_per_pulse_; ++sample) {
                const auto value = source[sample];
                (*output)(channel, pulse, sample) = pulse_compression_data::OutputSample{
                    ClampS16(value.re * scale), ClampS16(value.im * scale)};
            }
        }
    }
    return true;
}

std::size_t PulseCompressionAlgorithm::ReadSizeParam(const cycore::sdk::Params& params,
                                                      const std::string& key,
                                                      std::size_t fallback) {
    const auto value = params.get<std::int64_t>(key, static_cast<std::int64_t>(fallback));
    if (value <= 0) throw std::invalid_argument(key + " must be positive");
    const auto result = static_cast<std::size_t>(value);
    return result;
}

std::int16_t PulseCompressionAlgorithm::ClampS16(float value) {
    if (value >= 32767.0F) return 32767;
    if (value <= -32768.0F) return -32768;
    return static_cast<std::int16_t>(std::round(value));
}
