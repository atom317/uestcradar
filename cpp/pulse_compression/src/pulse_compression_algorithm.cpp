#include "pulse_compression_algorithm.h"

#include "pulse_compression_fft_backend.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kSampleRate = 30.72e6;
constexpr double kPulseWidth = 256.0 / kSampleRate;
constexpr double kBandwidth = 20e6;
constexpr double kChirpSlope = kBandwidth / kPulseWidth;
constexpr double kStartFreq = -10e6;
constexpr std::size_t kReplicaLength = 256;

#if defined(PULSE_COMPRESSION_WORKER_THREADS)
constexpr std::size_t kWorkerThreads = PULSE_COMPRESSION_WORKER_THREADS;
#else
constexpr std::size_t kWorkerThreads = 1;
#endif

#if defined(PULSE_COMPRESSION_CHUNK_SIZE)
constexpr std::size_t kConfiguredChunkSize = PULSE_COMPRESSION_CHUNK_SIZE;
#else
constexpr std::size_t kConfiguredChunkSize = 0;
#endif

std::size_t SelectChunkSize(std::size_t total_sequences) {
    if (kConfiguredChunkSize != 0) return std::min(kConfiguredChunkSize, total_sequences);
#if defined(PULSE_COMPRESSION_EXECUTION_OPENMP)
    return (total_sequences + kWorkerThreads - 1) / kWorkerThreads;
#else
    return total_sequences;
#endif
}

std::size_t FftwThreadCount() {
#if defined(PULSE_COMPRESSION_EXECUTION_FFTW_THREADS)
    return kWorkerThreads;
#else
    return 1;
#endif
}

} // namespace

namespace pulse_compression_detail {

struct PulseCompressionChunk {
    std::size_t first_sequence;
    std::unique_ptr<FftwBatchBackend> fft;
};

class PulseCompressionImplementation {
public:
    PulseCompressionImplementation(std::size_t channels, std::size_t pulses,
                                   std::size_t samples_per_pulse)
        : channels_(channels), pulses_(pulses), samples_per_pulse_(samples_per_pulse) {
        if (channels_ > std::numeric_limits<std::size_t>::max() / pulses_) {
            throw std::invalid_argument("pulse-compression sequence count overflow");
        }
        const std::size_t total_sequences = channels_ * pulses_;
        const std::size_t chunk_size = SelectChunkSize(total_sequences);
        for (std::size_t first = 0; first < total_sequences; first += chunk_size) {
            const std::size_t count = std::min(chunk_size, total_sequences - first);
            chunks_.push_back({first, std::make_unique<FftwBatchBackend>(
                                         samples_per_pulse_, count, FftwThreadCount())});
        }

        std::array<Complex32, kReplicaLength> impulse{};
        for (std::size_t m = 0; m < impulse.size(); ++m) {
            const double t = static_cast<double>(m) / kSampleRate;
            const double phase = 2.0 * kPi * kStartFreq * t + kPi * kChirpSlope * t * t;
            impulse[impulse.size() - 1 - m] = {
                static_cast<float>(std::cos(phase)), static_cast<float>(-std::sin(phase))};
        }
        for (auto& chunk : chunks_) {
            chunk.fft->build_matched_filter_spectrum(impulse.data(), impulse.size());
        }
    }

    bool work(cycore::sdk::Reader<pulse_compression_data::InputSample>& in,
              cycore::sdk::Writer<pulse_compression_data::OutputSample>& out) {
        auto input = in.read_cube(channels_, pulses_, samples_per_pulse_);
        if (!input) return false;
        auto output = out.reserve_cube(channels_, pulses_, samples_per_pulse_);
        if (!output) return false;

#if defined(PULSE_COMPRESSION_EXECUTION_OPENMP)
#pragma omp parallel for schedule(static) num_threads(PULSE_COMPRESSION_WORKER_THREADS)
#endif
        for (std::ptrdiff_t index = 0;
             index < static_cast<std::ptrdiff_t>(chunks_.size()); ++index) {
            ProcessChunk(*input, *output, chunks_[static_cast<std::size_t>(index)]);
        }
        return true;
    }

private:
    template <typename InputCube, typename OutputCube>
    void ProcessChunk(const InputCube& input, OutputCube& output,
                      PulseCompressionChunk& chunk) {
        auto& fft = *chunk.fft;
        const std::size_t fft_size = fft.fft_size();
        auto* work = fft.data();
        for (std::size_t local = 0; local < fft.batch_count(); ++local) {
            const std::size_t sequence = chunk.first_sequence + local;
            const std::size_t pulse = sequence / channels_;
            const std::size_t channel = sequence % channels_;
            auto* destination = work + local * fft_size;
            for (std::size_t sample = 0; sample < samples_per_pulse_; ++sample) {
                const auto value = input(channel, pulse, sample);
                destination[sample] = {static_cast<float>(value.i), static_cast<float>(value.q)};
            }
            std::fill(destination + samples_per_pulse_, destination + fft_size,
                      Complex32{0.0F, 0.0F});
        }

        fft.execute_forward();
        fft.multiply_by_filter();
        fft.execute_inverse();

        const float scale = 1.0F /
                            (static_cast<float>(fft_size) *
                             static_cast<float>(kReplicaLength));
        for (std::size_t local = 0; local < fft.batch_count(); ++local) {
            const std::size_t sequence = chunk.first_sequence + local;
            const std::size_t pulse = sequence / channels_;
            const std::size_t channel = sequence % channels_;
            const auto* source = work + local * fft_size +
                                 (kReplicaLength - 1);
            for (std::size_t sample = 0; sample < samples_per_pulse_; ++sample) {
                const auto value = source[sample];
                output(channel, pulse, sample) = pulse_compression_data::OutputSample{
                    Clamp(value.re * scale), Clamp(value.im * scale)};
            }
        }
    }

    static std::int16_t Clamp(float value) {
        if (value >= 32767.0F) return 32767;
        if (value <= -32768.0F) return -32768;
        return static_cast<std::int16_t>(value >= 0.0F ? value + 0.5F : value - 0.5F);
    }

    std::size_t channels_;
    std::size_t pulses_;
    std::size_t samples_per_pulse_;
    std::vector<PulseCompressionChunk> chunks_;
};

} // namespace pulse_compression_detail

PulseCompressionAlgorithm::PulseCompressionAlgorithm(const cycore::sdk::Params& params)
    : num_channels_(ReadSizeParam(
          params, "num_channels", pulse_compression_data::kDefaultNumChannels)),
      num_pulses_(ReadSizeParam(
          params, "num_pulses", pulse_compression_data::kDefaultNumPulses)),
      samples_per_pulse_(ReadSizeParam(
          params, "samples_per_pulse", pulse_compression_data::kDefaultSamplesPerPulse)),
      implementation_(std::make_unique<pulse_compression_detail::PulseCompressionImplementation>(
          num_channels_, num_pulses_, samples_per_pulse_)) {}

PulseCompressionAlgorithm::~PulseCompressionAlgorithm() = default;

bool PulseCompressionAlgorithm::work(
    cycore::sdk::Reader<pulse_compression_data::InputSample>& in,
    cycore::sdk::Writer<pulse_compression_data::OutputSample>& out) {
    return implementation_->work(in, out);
}

std::size_t PulseCompressionAlgorithm::ReadSizeParam(const cycore::sdk::Params& params,
                                                      const std::string& key,
                                                      std::size_t fallback) {
    const auto value = params.get<std::int64_t>(key, static_cast<std::int64_t>(fallback));
    if (value <= 0) throw std::invalid_argument(key + " must be positive");
    return static_cast<std::size_t>(value);
}
