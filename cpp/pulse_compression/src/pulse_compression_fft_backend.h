#pragma once

#include <cstddef>

namespace pulse_compression_detail {

struct Complex32 {
    float re;
    float im;
};

// Owns one reusable, in-place FFTW batch and its cached matched-filter
// spectrum.  Construction is the only allocation/planning point.
class FftwBatchBackend {
public:
    FftwBatchBackend(std::size_t samples_per_pulse, std::size_t batch_count);
    ~FftwBatchBackend();

    FftwBatchBackend(const FftwBatchBackend&) = delete;
    FftwBatchBackend& operator=(const FftwBatchBackend&) = delete;

    std::size_t fft_size() const noexcept { return fft_size_; }
    std::size_t batch_count() const noexcept { return batch_count_; }
    Complex32* data() noexcept { return data_; }

    void build_matched_filter_spectrum(const Complex32* impulse_response,
                                       std::size_t impulse_length);
    void execute_forward() const;
    void multiply_by_filter() noexcept;
    void execute_inverse() const;

private:
    std::size_t fft_size_;
    std::size_t batch_count_;
    Complex32* data_;
    Complex32* filter_spectrum_;
    void* forward_plan_;
    void* inverse_plan_;
};

std::size_t SelectFftSize(std::size_t samples_per_pulse,
                          std::size_t impulse_length);

} // namespace pulse_compression_detail
