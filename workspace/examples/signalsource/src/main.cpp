#include <data.h>
#include <sdk.h>

#include "my_waveform.hpp"

#include <chrono>
#include <complex>
#include <cstdint>
#include <iostream>
#include <vector>

int main() {
    try {
        using namespace uestcradar;

        Output<IQFrame> output;
        std::vector<std::complex<float>> waveform(1024);
        RadarSource::generate_sine_wave(waveform);
        std::uint64_t frame_id = 0;
        std::cout << "成功生成模拟 IQ 波形，开始持续发送...\n";

        for (;;) {
            const auto now = std::chrono::system_clock::now();
            auto frame = output.create({
                .frame_id = ++frame_id,
                .timestamp_unix_ns =
                    static_cast<std::uint64_t>(
                        std::chrono::duration_cast<
                            std::chrono::nanoseconds>(
                            now.time_since_epoch())
                            .count()),
                .channel_count = 1,
                .samples_per_channel =
                    static_cast<std::uint32_t>(waveform.size()),
                .sample_rate_hz = 1000.0,
                .center_frequency_hz = 0.0,
            });
            for (std::size_t index = 0;
                 index < waveform.size();
                 ++index) {
                frame.data[0][index] = {
                    static_cast<std::int16_t>(
                        waveform[index].real() * 30'000.0F),
                    static_cast<std::int16_t>(
                        waveform[index].imag() * 30'000.0F),
                };
            }
            output.write(frame);
        }
    } catch (const std::exception& error) {
        std::cerr << "发送 IQ 数据失败：" << error.what() << '\n';
        return 1;
    }
}
