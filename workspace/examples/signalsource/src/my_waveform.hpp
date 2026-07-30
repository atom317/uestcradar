#pragma once

#include <vector>
#include <complex>
#include <cmath>
#include <iostream>

namespace RadarSource {

    // 内部隐藏的波形生成逻辑 (100Hz 正弦波)
    inline void generate_sine_wave(std::vector<std::complex<float>>& frame) {
        const float fs = 1000.0f; // 采样率
        const float f0 = 100.0f;  // 正弦波频率
        const size_t N = frame.size();

        for (size_t i = 0; i < N; ++i) {
            float t = static_cast<float>(i) / fs;
            frame[i] = std::polar(1.0f, 2.0f * static_cast<float>(M_PI) * f0 * t);
        }
    }
}
