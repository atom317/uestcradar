#include <flowgraph/blocks/common/lfm_source.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <string>

namespace cy::flowgraph::blocks::common {

namespace {

constexpr double kTwoPi = 6.28318530717958647692;

std::int16_t Quantize(double val) {
    double rounded = std::round(val);
    if (rounded < -32768.0) return -32768;
    if (rounded > 32767.0) return 32767;
    return static_cast<std::int16_t>(rounded);
}

std::size_t SizeParam(const cy::flowgraph::ValueMap& params,
                      const char* name,
                      std::int64_t fallback) {
    const auto value = cy::flowgraph::value_or<std::int64_t>(params, name, fallback);
    if (value <= 0) {
        throw std::invalid_argument(std::string("LFMSource ") + name +
                                    " must be greater than zero");
    }
    return static_cast<std::size_t>(value);
}

double DoubleParam(const cy::flowgraph::ValueMap& params,
                   const char* name,
                   double fallback) {
    return cy::flowgraph::value_or<double>(params, name, fallback);
}

std::string StringParam(const cy::flowgraph::ValueMap& params,
                        const char* name,
                        const char* fallback) {
    return cy::flowgraph::value_or<std::string>(params, name, fallback);
}

} // namespace

LFMSource::LFMSource()
    : LFMSource(cy::flowgraph::ValueMap{}) {}

LFMSource::LFMSource(const cy::flowgraph::ValueMap& params)
    : num_channels_(SizeParam(params, "num_channels", 8)),
      samples_per_pulse_(SizeParam(params, "samples_per_pulse", 4096)),
      batch_size_(SizeParam(params, "batch_size", 128)),
      waveform_type_(StringParam(params, "waveform_type", "lfm")),
      sample_rate_(DoubleParam(params, "sample_rate", 30.72e6)),
      tone_frequency_(DoubleParam(params, "tone_frequency", 960.0e3)),
      pulse_width_samples_(DoubleParam(params, "pulse_width_samples", 256.0)),
      bandwidth_(DoubleParam(params, "bandwidth", 20.0e6)),
      start_freq_(DoubleParam(params, "start_frequency", -10.0e6)),
      amplitude_(DoubleParam(params, "amplitude", 8191.0)),
      range_bin_(DoubleParam(params, "range_bin", 100.0)),
      channel_phase_step_(DoubleParam(params, "channel_phase_step", 0.5)) {
    if (batch_size_ < num_channels_ || batch_size_ % num_channels_ != 0) {
        throw std::invalid_argument(
            "LFMSource batch_size must be a positive multiple of num_channels");
    }
    if (sample_rate_ <= 0.0 || amplitude_ < 0.0 || amplitude_ > 8191.0) {
        throw std::invalid_argument("LFMSource waveform parameters are out of range");
    }
    if (samples_per_pulse_ >
        std::numeric_limits<std::size_t>::max() / num_channels_) {
        throw std::overflow_error("LFMSource pulse element count overflows size_t");
    }
    pulse_elements_ = samples_per_pulse_ * num_channels_;

    if (waveform_type_ == "lfm") {
        if (pulse_width_samples_ <= 0.0 || bandwidth_ <= 0.0 ||
            range_bin_ < 0.0 ||
            range_bin_ + pulse_width_samples_ > samples_per_pulse_) {
            throw std::invalid_argument("LFMSource LFM parameters are out of range");
        }
        waveform_template_.resize(pulse_elements_, cy::common::CS16{0, 0});
        const double start_sample = range_bin_;
        const double end_sample = range_bin_ + pulse_width_samples_;
        const double chirp_slope = bandwidth_ * sample_rate_ / pulse_width_samples_;
        for (std::size_t sample = 0; sample < samples_per_pulse_; ++sample) {
            const double sample_d = static_cast<double>(sample);
            if (sample_d < start_sample || sample_d >= end_sample) {
                continue;
            }
            const double t_in_pulse = (sample_d - start_sample) / sample_rate_;
            const double lfm_phase =
                kTwoPi * start_freq_ * t_in_pulse +
                3.14159265358979323846 * chirp_slope * t_in_pulse * t_in_pulse;
            for (std::size_t channel = 0; channel < num_channels_; ++channel) {
                const double phase =
                    lfm_phase + channel_phase_step_ * static_cast<double>(channel);
                waveform_template_[sample * num_channels_ + channel] =
                    cy::common::CS16{
                        Quantize(amplitude_ * std::cos(phase)),
                        Quantize(amplitude_ * std::sin(phase))};
            }
        }
    } else if (waveform_type_ == "tone") {
        if (std::abs(tone_frequency_) >= sample_rate_ / 2.0) {
            throw std::invalid_argument("LFMSource tone_frequency exceeds Nyquist");
        }
        const std::size_t samples_per_batch = batch_size_ / num_channels_;
        const double cycles_per_batch =
            tone_frequency_ * static_cast<double>(samples_per_batch) / sample_rate_;
        if (std::abs(cycles_per_batch - std::round(cycles_per_batch)) > 1.0e-9) {
            throw std::invalid_argument(
                "LFMSource tone must contain an integer number of cycles per batch");
        }
        waveform_template_.resize(batch_size_);
        const double phase_step = kTwoPi * tone_frequency_ / sample_rate_;
        for (std::size_t sample = 0; sample < samples_per_batch; ++sample) {
            for (std::size_t channel = 0; channel < num_channels_; ++channel) {
                const double phase =
                    phase_step * static_cast<double>(sample) +
                    channel_phase_step_ * static_cast<double>(channel);
                waveform_template_[sample * num_channels_ + channel] =
                    cy::common::CS16{
                        Quantize(amplitude_ * std::cos(phase)),
                        Quantize(amplitude_ * std::sin(phase))};
            }
        }
    } else if (waveform_type_ == "constant") {
        waveform_template_.resize(batch_size_);
        const std::size_t samples_per_batch = batch_size_ / num_channels_;
        for (std::size_t sample = 0; sample < samples_per_batch; ++sample) {
            for (std::size_t channel = 0; channel < num_channels_; ++channel) {
                const double phase =
                    channel_phase_step_ * static_cast<double>(channel);
                waveform_template_[sample * num_channels_ + channel] =
                    cy::common::CS16{
                        Quantize(amplitude_ * std::cos(phase)),
                        Quantize(amplitude_ * std::sin(phase))};
            }
        }
    } else {
        throw std::invalid_argument(
            "LFMSource waveform_type must be lfm, tone, or constant");
    }

    if (!waveform_template_.empty() && batch_size_ % waveform_template_.size() == 0) {
        const std::size_t repeat_count = batch_size_ / waveform_template_.size();
        batch_template_.resize(batch_size_);
        for (std::size_t i = 0; i < repeat_count; ++i) {
            std::memcpy(batch_template_.data() + i * waveform_template_.size(),
                        waveform_template_.data(),
                        waveform_template_.size() * sizeof(cy::common::CS16));
        }
    }
}

void LFMSource::on_start() {
    reset_stats();
    std::fprintf(stderr,
                 "[TXTRACE][LFM] started waveform=%s channels=%zu batch_elements=%zu per_channel=%zu template_elements=%zu repeat=continuous generation=precomputed layout=sample_major_interleaved\n",
                 waveform_type_.c_str(), num_channels_, batch_size_,
                 batch_size_ / num_channels_, waveform_template_.size());
}

void LFMSource::process_work() {
    ++process_work_calls_;
    std::size_t available = out.available();
    if (available == 0) {
        return;
    }

    std::size_t batch = std::min(available, batch_size_);
    batch -= batch % num_channels_;
    if (batch == 0) {
        return;
    }

    auto span = out.reserve(batch);
    if (span.empty()) {
        return;
    }

    if (!batch_template_.empty() && template_offset_ == 0 && span.size() == batch_template_.size()) {
        std::memcpy(span.data(), batch_template_.data(), batch_template_.size() * sizeof(cy::common::CS16));
        ++fast_path_hits_;
    } else {
        std::size_t copied = 0;
        std::size_t source_offset = template_offset_;
        while (copied < span.size()) {
            const std::size_t chunk =
                std::min(span.size() - copied, waveform_template_.size() - source_offset);
            std::memcpy(span.data() + copied,
                        waveform_template_.data() + source_offset,
                        chunk * sizeof(cy::common::CS16));
            copied += chunk;
            source_offset = 0;
            ++fallback_memcpy_count_;
        }
    }

    if (!first_nonzero_logged_) {
        for (std::size_t i = 0; i < span.size(); ++i) {
            if (span[i].i == 0 && span[i].q == 0) {
                continue;
            }
            const std::size_t template_index =
                (template_offset_ + i) % waveform_template_.size();
            first_nonzero_logged_ = true;
            std::fprintf(stderr,
                         "[TXTRACE][LFM] first_nonzero template_element=%zu sample=%zu channel=%zu iq=(%d,%d)\n",
                         template_index, template_index / num_channels_,
                         template_index % num_channels_, static_cast<int>(span[i].i),
                         static_cast<int>(span[i].q));
            break;
        }
    }

    const std::size_t advanced = template_offset_ + span.size();
    const std::uint64_t completed = advanced / waveform_template_.size();
    template_offset_ = advanced % waveform_template_.size();
    repeated_templates_ += completed;
    span.commit(span.size());

    if (!first_publish_logged_) {
        first_publish_logged_ = true;
        std::fprintf(stderr,
                     "[TXTRACE][LFM] first_publish waveform=%s commit_elements=%zu per_channel=%zu template_offset=%zu/%zu\n",
                     waveform_type_.c_str(), span.size(), span.size() / num_channels_,
                     template_offset_, waveform_template_.size());
    }
    if (completed != 0 && repeated_templates_ == completed) {
        std::fprintf(stderr,
                     "[TXTRACE][LFM] first_template_repeated waveform=%s template_elements=%zu continuous=1\n",
                     waveform_type_.c_str(), waveform_template_.size());
    }
}

} // namespace cy::flowgraph::blocks::common
