#pragma once

#include <vector>
#include <complex>
#include <cmath>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <cstdint>
#include <string>

namespace RadarAlgo {

    // 内部隐藏的复杂 FFT 算法实现
    inline void _dft(const std::vector<std::complex<float>>& x, std::vector<float>& mag) {
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
        int W = mag.size();
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

    // 暴露给外部的极其简单的算法接口
    inline void process_fft_and_save(const std::vector<std::complex<float>>& frame_data, const std::string& output_path) {
        std::cout << "[算法黑盒] 正在执行傅里叶变换 (FFT)...\n";
        std::vector<float> mag;
        _dft(frame_data, mag);
        
        std::cout << "[算法黑盒] 正在将结果保存为图片: " << output_path << "...\n";
        _save_pgm(mag, output_path);
    }
}
