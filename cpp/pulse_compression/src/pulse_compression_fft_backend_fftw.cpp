#include "pulse_compression_fft_backend.h"

#include <fftw3.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <type_traits>

namespace pulse_compression_detail {
namespace {

std::mutex& PlannerMutex() {
    static std::mutex mutex;
    return mutex;
}

void EnsureFftwThreadsInitialized() {
#if defined(PULSE_COMPRESSION_EXECUTION_FFTW_THREADS)
    static std::once_flag initialized;
    std::call_once(initialized, [] {
        if (fftwf_init_threads() == 0) {
            throw std::runtime_error("failed to initialize FFTW threads");
        }
    });
#endif
}

[[maybe_unused]] bool IsSmooth2357(std::size_t value) {
    for (const std::size_t factor : {std::size_t{2}, std::size_t{3}, std::size_t{5},
                                     std::size_t{7}}) {
        while (value % factor == 0) value /= factor;
    }
    return value == 1;
}

[[maybe_unused]] std::size_t NextPowerOfTwo(std::size_t value) {
    std::size_t result = 1;
    while (result < value) {
        if (result > std::numeric_limits<std::size_t>::max() / 2) {
            throw std::invalid_argument("FFT size overflow");
        }
        result <<= 1U;
    }
    return result;
}

fftwf_plan AsPlan(void* plan) { return reinterpret_cast<fftwf_plan>(plan); }

static_assert(sizeof(Complex32) == sizeof(fftwf_complex),
              "Complex32 must match the FFTW single-precision complex layout");
static_assert(alignof(Complex32) == alignof(fftwf_complex),
              "Complex32 must match the FFTW single-precision complex alignment");
static_assert(std::is_standard_layout<Complex32>::value,
              "Complex32 must remain a standard-layout buffer element");

} // namespace

std::size_t SelectFftSize(std::size_t samples_per_pulse,
                          std::size_t impulse_length) {
    if (samples_per_pulse == 0 || impulse_length == 0 ||
        samples_per_pulse > std::numeric_limits<std::size_t>::max() -
                               (impulse_length - 1)) {
        throw std::invalid_argument("invalid pulse-compression FFT dimensions");
    }
    const std::size_t required = samples_per_pulse + impulse_length - 1;

#if defined(PULSE_COMPRESSION_NFFT_MODE_POW2)
    return NextPowerOfTwo(required);
#elif defined(PULSE_COMPRESSION_NFFT_MODE_EXPLICIT)
    constexpr std::size_t kExplicitNfft = PULSE_COMPRESSION_EXPLICIT_NFFT;
    if (kExplicitNfft < required) {
        throw std::invalid_argument("explicit NFFT is smaller than linear convolution");
    }
    return kExplicitNfft;
#else
    // FFTW efficiently handles composite non-power-of-two transforms.  Keep
    // this deterministic; runtime auto-tuning would make plugin creation slow.
    for (std::size_t candidate = required;; ++candidate) {
        if (IsSmooth2357(candidate)) return candidate;
        if (candidate == std::numeric_limits<std::size_t>::max()) {
            throw std::invalid_argument("FFT size overflow");
        }
    }
#endif
}

FftwBatchBackend::FftwBatchBackend(std::size_t samples_per_pulse,
                                   std::size_t impulse_length,
                                   std::size_t batch_count,
                                   std::size_t fftw_thread_count)
    : fft_size_(SelectFftSize(samples_per_pulse, impulse_length)),
      batch_count_(batch_count),
      data_(nullptr),
      filter_spectrum_(nullptr),
      forward_plan_(nullptr),
      inverse_plan_(nullptr) {
    if (batch_count_ == 0 || fft_size_ > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        batch_count_ > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        batch_count_ > std::numeric_limits<std::size_t>::max() / fft_size_) {
        throw std::invalid_argument("invalid FFT batch dimensions");
    }

    const std::size_t total = fft_size_ * batch_count_;
    data_ = static_cast<Complex32*>(fftwf_malloc(sizeof(Complex32) * total));
    filter_spectrum_ = static_cast<Complex32*>(fftwf_malloc(sizeof(Complex32) * fft_size_));
    if (data_ == nullptr || filter_spectrum_ == nullptr) {
        fftwf_free(data_);
        fftwf_free(filter_spectrum_);
        throw std::bad_alloc();
    }
    std::fill_n(data_, total, Complex32{0.0F, 0.0F});
    std::fill_n(filter_spectrum_, fft_size_, Complex32{0.0F, 0.0F});

    int length = static_cast<int>(fft_size_);
    std::lock_guard<std::mutex> lock(PlannerMutex());
    EnsureFftwThreadsInitialized();
#if defined(PULSE_COMPRESSION_EXECUTION_FFTW_THREADS)
    fftwf_plan_with_nthreads(static_cast<int>(fftw_thread_count));
#else
    (void)fftw_thread_count;
#endif
#if defined(PULSE_COMPRESSION_FFTW_PLAN_MEASURE)
    constexpr unsigned kPlanFlags = FFTW_MEASURE;
#else
    constexpr unsigned kPlanFlags = FFTW_ESTIMATE;
#endif
    forward_plan_ = fftwf_plan_many_dft(
        1, &length, static_cast<int>(batch_count_),
        reinterpret_cast<fftwf_complex*>(data_), nullptr, 1, static_cast<int>(fft_size_),
        reinterpret_cast<fftwf_complex*>(data_), nullptr, 1, static_cast<int>(fft_size_),
        FFTW_FORWARD, kPlanFlags);
    inverse_plan_ = fftwf_plan_many_dft(
        1, &length, static_cast<int>(batch_count_),
        reinterpret_cast<fftwf_complex*>(data_), nullptr, 1, static_cast<int>(fft_size_),
        reinterpret_cast<fftwf_complex*>(data_), nullptr, 1, static_cast<int>(fft_size_),
        FFTW_BACKWARD, kPlanFlags);
    if (forward_plan_ == nullptr || inverse_plan_ == nullptr) {
        if (forward_plan_ != nullptr) fftwf_destroy_plan(AsPlan(forward_plan_));
        if (inverse_plan_ != nullptr) fftwf_destroy_plan(AsPlan(inverse_plan_));
        fftwf_free(data_);
        fftwf_free(filter_spectrum_);
        throw std::runtime_error("failed to create FFTW plans");
    }
}

FftwBatchBackend::~FftwBatchBackend() {
    std::lock_guard<std::mutex> lock(PlannerMutex());
    if (forward_plan_ != nullptr) fftwf_destroy_plan(AsPlan(forward_plan_));
    if (inverse_plan_ != nullptr) fftwf_destroy_plan(AsPlan(inverse_plan_));
    fftwf_free(data_);
    fftwf_free(filter_spectrum_);
    // Do not call fftwf_cleanup(): it is process-global and can invalidate
    // FFTW plans owned by other algorithm instances.
}

void FftwBatchBackend::build_matched_filter_spectrum(const Complex32* impulse_response,
                                                     std::size_t impulse_length) {
    if (impulse_length > fft_size_) {
        throw std::invalid_argument("matched filter exceeds FFT size");
    }
    const std::size_t total = fft_size_ * batch_count_;
    std::fill_n(data_, total, Complex32{0.0F, 0.0F});
    std::copy_n(impulse_response, impulse_length, data_);
    execute_forward();
    std::copy_n(data_, fft_size_, filter_spectrum_);
    std::fill_n(data_, total, Complex32{0.0F, 0.0F});
}

void FftwBatchBackend::execute_forward() const { fftwf_execute(AsPlan(forward_plan_)); }

void FftwBatchBackend::multiply_by_filter() noexcept {
    for (std::size_t sequence = 0; sequence < batch_count_; ++sequence) {
        auto* spectrum = data_ + sequence * fft_size_;
        for (std::size_t bin = 0; bin < fft_size_; ++bin) {
            const Complex32 value = spectrum[bin];
            const Complex32 filter = filter_spectrum_[bin];
            spectrum[bin] = {value.re * filter.re - value.im * filter.im,
                             value.re * filter.im + value.im * filter.re};
        }
    }
}

void FftwBatchBackend::execute_inverse() const { fftwf_execute(AsPlan(inverse_plan_)); }

} // namespace pulse_compression_detail
