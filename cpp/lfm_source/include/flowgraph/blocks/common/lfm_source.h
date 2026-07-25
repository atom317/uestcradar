#ifndef CYCORE_FLOWGRAPH_LFM_SOURCE_H
#define CYCORE_FLOWGRAPH_LFM_SOURCE_H

#include <flowgraph/block.h>
#include <flowgraph/port.h>
#include <flowgraph/value.h>
#include <common/data_types.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cy::flowgraph::blocks::common {

class LFMSource : public cy::flowgraph::Block<LFMSource> {
public:
    cy::flowgraph::PortOut<cy::common::CS16> out;
    CY_MAKE_REFLECTABLE(LFMSource, out);

    LFMSource();
    explicit LFMSource(const cy::flowgraph::ValueMap& params);
    void on_start();
    bool process_work();

    std::size_t num_channels() const noexcept { return num_channels_; }
    std::size_t total_elements() const noexcept { return pulse_elements_; }

    std::uint64_t process_work_calls() const noexcept { return process_work_calls_; }
    std::uint64_t fast_path_hits() const noexcept { return fast_path_hits_; }
    std::uint64_t fallback_memcpy_count() const noexcept { return fallback_memcpy_count_; }
    void reset_stats() noexcept {
        process_work_calls_ = 0;
        fast_path_hits_ = 0;
        fallback_memcpy_count_ = 0;
    }

private:
    std::size_t template_offset_ = 0;
    std::size_t num_channels_ = 8;
    std::size_t samples_per_pulse_ = 4096;
    std::size_t batch_size_ = 128;
    std::size_t pulse_elements_ = 4096;
    std::vector<cy::common::CS16> waveform_template_;
    std::vector<cy::common::CS16> batch_template_;
    std::uint64_t repeated_templates_ = 0;
    std::string waveform_type_ = "lfm";
    double sample_rate_ = 30.72e6;
    double tone_frequency_ = 960.0e3;
    double pulse_width_samples_ = 256.0;
    double bandwidth_ = 20.0e6;
    double start_freq_ = -10.0e6;
    double amplitude_ = 8191.0;
    double range_bin_ = 100.0;
    double channel_phase_step_ = 0.5;
    bool first_publish_logged_ = false;
    bool first_nonzero_logged_ = false;

    std::uint64_t process_work_calls_ = 0;
    std::uint64_t fast_path_hits_ = 0;
    std::uint64_t fallback_memcpy_count_ = 0;
};

} // namespace cy::flowgraph::blocks::common
#endif
