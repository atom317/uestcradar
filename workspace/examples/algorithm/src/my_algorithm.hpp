#pragma once

#include <data.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace RadarAlgo {

    // 格式化打印 PulseCompressionFrame (PCFrame) 的元数据与样本数据
    inline void print_pc_frame(const uestcradar::PulseCompressionFrame& pc_frame) {
        std::cout << "\n==================== [PCFrame 脉冲压缩帧信息] ====================\n";
        std::cout << "  - 帧 ID (frame_id)            : " << pc_frame.metadata.frame_id << "\n";
        std::cout << "  - 时间戳 (timestamp_unix_ns) : " << pc_frame.metadata.timestamp_unix_ns << " ns\n";
        std::cout << "  - 通道数 (channel_count)      : " << pc_frame.metadata.channel_count << "\n";
        std::cout << "  - 距离门数 (range_bin_count)  : " << pc_frame.metadata.range_bin_count << "\n";
        std::cout << "  - 脉冲索引 (pulse_index)      : " << pc_frame.metadata.pulse_index << "\n";
        std::cout << "  - CPI 内脉冲数 (pulses_per_cpi): " << pc_frame.metadata.pulses_per_cpi << "\n";
        std::cout << "  - 距离分辨率 (range_res_m)    : " << pc_frame.metadata.range_resolution_m << " m\n";

        if (pc_frame.metadata.channel_count > 0 && pc_frame.metadata.range_bin_count > 0) {
            std::cout << "  - Channel 0 前 5 个采样点 (ComplexFloat32):\n";
            std::size_t print_count = std::min<std::size_t>(5, pc_frame.metadata.range_bin_count);
            for (std::size_t i = 0; i < print_count; ++i) {
                const auto& sample = pc_frame.data[0][i];
                float mag = std::sqrt(sample.i * sample.i + sample.q * sample.q);
                std::cout << "      [bin " << i << "] I: " << sample.i
                          << ", Q: " << sample.q << " | 幅值: " << mag << "\n";
            }
        }
        std::cout << "===================================================================\n\n";
    }

    // 内部隐藏的脉冲压缩 (Pulse Compression) 算法实现
    inline void _pulse_compression(const std::vector<std::complex<float>>& x, std::vector<float>& mag) {
        const size_t N = x.size();
        mag.resize(N);
        for (size_t k = 0; k < N; ++k) {
            std::complex<float> X_k(0, 0);
            for (size_t n = 0; n < N; ++n) {
                float angle = -2.0f * static_cast<float>(M_PI) * k * n / N;
                X_k += x[n] * std::polar(1.0f, angle);
            }
            mag[k] = std::abs(X_k);
        }
    }

    // 内部隐藏的图片导出实现
    inline void _save_pgm(const std::vector<float>& mag, const std::string& filename) {
        int W = static_cast<int>(mag.size());
        int H = 256;
        std::vector<uint8_t> img(W * H, 255);
        float max_val = *std::max_element(mag.begin(), mag.end());
        if (max_val < 1e-5f) max_val = 1.0f;
        for (int x = 0; x < W; ++x) {
            int h = static_cast<int>((mag[x] / max_val) * (H - 1));
            for (int y = H - 1; y >= H - 1 - h; --y) {
                img[y * W + x] = 0;
            }
        }
        std::ofstream ofs(filename, std::ios::binary);
        ofs << "P5\n" << W << " " << H << "\n255\n";
        ofs.write(reinterpret_cast<const char*>(img.data()), img.size());
    }

    // 暴露给外部的算法接口，接收 PCFrame 并在内部对其打印与导出
    inline void process_pulse_compression_and_save(
        const uestcradar::PulseCompressionFrame& pc_frame,
        const std::string& output_path) {
        
        // 每次计算/生成 PCFrame 后，调用打印函数
        print_pc_frame(pc_frame);

        std::cout << "[算法黑盒] 正在将脉冲压缩结果保存为图片: " << output_path << "...\n";
        
        std::vector<std::complex<float>> first_channel;
        if (pc_frame.metadata.channel_count > 0) {
            first_channel.reserve(pc_frame.metadata.range_bin_count);
            for (std::size_t i = 0; i < pc_frame.metadata.range_bin_count; ++i) {
                const auto& sample = pc_frame.data[0][i];
                first_channel.emplace_back(sample.i, sample.q);
            }
        }

        std::vector<float> mag;
        _pulse_compression(first_channel, mag);
        _save_pgm(mag, output_path);
    }
}
