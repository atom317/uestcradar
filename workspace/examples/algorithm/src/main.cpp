#include <data.h>
#include <sdk.h>

#include "my_algorithm.hpp"

#include <complex>
#include <iostream>
#include <vector>

int main() {
    try {
        using namespace uestcradar;

        Input<IQFrame> input;
        Output<PulseCompressionFrame> output;
        std::cout << "成功接入 IQ 数据源，持续接收数据中...\n";

        while (true) {
            auto iq = input.read();
            auto pulse = output.create({
                .frame_id = iq.metadata.frame_id,
                .timestamp_unix_ns = iq.metadata.timestamp_unix_ns,
                .channel_count = iq.metadata.channel_count,
                .range_bin_count = iq.metadata.samples_per_channel,
                .pulse_index = 0,
                .pulses_per_cpi = 1,
                .range_resolution_m = 1.0,
            });

            for (std::size_t channel = 0;
                 channel < iq.data.rows();
                 ++channel) {
                for (std::size_t sample = 0;
                     sample < iq.data.columns();
                     ++sample) {
                    const auto value = iq.data[channel][sample];
                    pulse.data[channel][sample] = {
                        static_cast<float>(value.i),
                        static_cast<float>(value.q),
                    };
                }
            }
            output.write(pulse);

            RadarAlgo::process_pulse_compression_and_save(
                pulse, "/output/pulse_compression_result.pgm");
        }
    } catch (const std::exception& error) {
        std::cerr << "算法处理失败：" << error.what() << '\n';
        return 1;
    }
    return 0;
}
