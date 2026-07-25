#pragma once

#include "data.h"

#include <cycore_algorithm_sdk.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <vector>

namespace rd_data = cycore::algorithm::range_doppler;

class RangeDopplerAlgorithm {
public:
    explicit RangeDopplerAlgorithm(const cycore::sdk::Params& params)
        : num_channels_(ReadSizeParam(params, "num_channels", rd_data::kDefaultNumChannels)),
          num_pulses_(ReadSizeParam(params, "num_pulses", rd_data::kDefaultNumPulses)),
          samples_per_pulse_(ReadSizeParam(params, "samples_per_pulse", rd_data::kDefaultSamplesPerPulse)) {

        if (num_channels_ == 0 || num_pulses_ == 0 || samples_per_pulse_ == 0) {
            throw std::invalid_argument("Invalid Range-Doppler dimensions");
        }

        // Radix-2 FFT 要求 num_pulses 必须是 2 的幂
        if ((num_pulses_ & (num_pulses_ - 1)) != 0) {
            throw std::invalid_argument(
                "num_pulses must be a power of 2 for Radix-2 FFT (got " +
                std::to_string(num_pulses_) + ")");
        }

        log2_n_ = 0;
        for (std::size_t v = num_pulses_; v > 1; v >>= 1) {
            ++log2_n_;
        }

        // ── 预计算位反转表 ──
        bit_rev_.resize(num_pulses_);
        for (std::size_t i = 0; i < num_pulses_; ++i) {
            std::size_t rev = 0;
            std::size_t val = i;
            for (std::size_t b = 0; b < log2_n_; ++b) {
                rev = (rev << 1) | (val & 1);
                val >>= 1;
            }
            bit_rev_[i] = rev;
        }

        // ── 预计算 FFT 蝶形旋转因子 W_N^k = e^{-j2πk/N} ──
        const double pi = 3.14159265358979323846;
        twiddles_.resize(num_pulses_ / 2);
        for (std::size_t i = 0; i < num_pulses_ / 2; ++i) {
            double angle = -2.0 * pi * static_cast<double>(i) /
                           static_cast<double>(num_pulses_);
            twiddles_[i] = {static_cast<float>(std::cos(angle)),
                            static_cast<float>(std::sin(angle))};
        }

        // ── 预分配转置工作缓冲区 [samples_per_pulse][num_pulses] ──
        transpose_buf_.resize(samples_per_pulse_ * num_pulses_);
    }

    bool work(cycore::sdk::Reader<rd_data::InputSample>& in,
              cycore::sdk::Writer<rd_data::OutputSample>& out) {
        auto input = in.read_cube(num_channels_, num_pulses_, samples_per_pulse_);
        if (!input) {
            return false;
        }

        // 输出仅 1 通道 — 只处理 channel 0 的多普勒 FFT
        auto output = out.reserve_cube(1, num_pulses_, samples_per_pulse_);
        if (!output) {
            return false;
        }

        const float inv_n = 1.0f / static_cast<float>(num_pulses_);

        {
            constexpr std::size_t ch = 0;
            // ═══════════════════════════════════════════════════
            // Phase 1: 转置 — [pulse][sample] → [sample][pulse]
            //   使 FFT 在连续内存上操作，消除 cache miss
            // ═══════════════════════════════════════════════════
            for (std::size_t n = 0; n < num_pulses_; ++n) {
                for (std::size_t s = 0; s < samples_per_pulse_; ++s) {
                    auto x = (*input)(ch, n, s);
                    transpose_buf_[s * num_pulses_ + n] = {
                        static_cast<float>(x.i),
                        static_cast<float>(x.q)};
                }
            }

            // ═══════════════════════════════════════════════════
            // Phase 2: 对每个距离单元执行 in-place Radix-2 FFT
            //   复杂度: O(N·log₂N) vs 原 O(N²)
            // ═══════════════════════════════════════════════════
            for (std::size_t s = 0; s < samples_per_pulse_; ++s) {
                CF32* row = &transpose_buf_[s * num_pulses_];
                fft_inplace(row);
            }

            // ═══════════════════════════════════════════════════
            // Phase 3: 功率谱 → dB 转换
            //   跳过 sqrt，直接用 10·log₁₀(power/N) 等价替代
            //   20·log₁₀(|X|/√N) = 10·log₁₀(|X|²/N)
            // ═══════════════════════════════════════════════════
            for (std::size_t s = 0; s < samples_per_pulse_; ++s) {
                const CF32* row = &transpose_buf_[s * num_pulses_];
                for (std::size_t k = 0; k < num_pulses_; ++k) {
                    float power =
                        (row[k].re * row[k].re + row[k].im * row[k].im) * inv_n;
                    std::size_t k_shifted = (k + num_pulses_ / 2) % num_pulses_;
                    (*output)(0, k_shifted, s) =
                        10.0f * std::log10(std::max(power, 1e-12f));
                }
            }
        }

        return true;
    }

private:
    /// 单精度复数
    struct CF32 {
        float re;
        float im;
    };

    /// In-place Radix-2 Cooley-Tukey FFT（DIT，时域抽取）
    void fft_inplace(CF32* data) const {
        const std::size_t n = num_pulses_;

        // ── 位反转重排 ──
        for (std::size_t i = 0; i < n; ++i) {
            std::size_t j = bit_rev_[i];
            if (j > i) {
                CF32 tmp = data[i];
                data[i] = data[j];
                data[j] = tmp;
            }
        }

        // ── 蝶形运算（log₂N 级） ──
        for (std::size_t stage = 0; stage < log2_n_; ++stage) {
            const std::size_t half = std::size_t{1} << stage;
            const std::size_t span = half << 1;
            const std::size_t tw_stride = n / span;

            for (std::size_t group = 0; group < n; group += span) {
                for (std::size_t b = 0; b < half; ++b) {
                    const CF32& w = twiddles_[b * tw_stride];
                    CF32& even = data[group + b];
                    CF32& odd = data[group + b + half];

                    // 蝶形乘法: T = odd * W
                    float tr = odd.re * w.re - odd.im * w.im;
                    float ti = odd.re * w.im + odd.im * w.re;

                    // 蝶形加减
                    odd.re = even.re - tr;
                    odd.im = even.im - ti;
                    even.re = even.re + tr;
                    even.im = even.im + ti;
                }
            }
        }
    }

    static std::size_t ReadSizeParam(const cycore::sdk::Params& params,
                                     const std::string& key,
                                     std::size_t fallback) {
        const auto value =
            params.get<std::int64_t>(key, static_cast<std::int64_t>(fallback));
        if (value <= 0) {
            throw std::invalid_argument(key + " must be positive");
        }
        return static_cast<std::size_t>(value);
    }

    std::size_t num_channels_;
    std::size_t num_pulses_;
    std::size_t samples_per_pulse_;
    std::size_t log2_n_;
    std::vector<std::size_t> bit_rev_;
    std::vector<CF32> twiddles_;
    std::vector<CF32> transpose_buf_;
};
