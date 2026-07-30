#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <type_traits>

namespace uestcradar {

struct ComplexInt16 {
    std::int16_t i{};
    std::int16_t q{};
};

struct ComplexFloat32 {
    float i{};
    float q{};
};

template <class T>
class Array2D {
public:
    Array2D() noexcept = default;

    Array2D(
        T* values,
        std::size_t rows,
        std::size_t columns) noexcept
        : values_(values), rows_(rows), columns_(columns) {}

    [[nodiscard]] std::size_t rows() const noexcept {
        return rows_;
    }

    [[nodiscard]] std::size_t columns() const noexcept {
        return columns_;
    }

    [[nodiscard]] std::span<T> operator[](
        std::size_t row) const noexcept {
        return {values_ + row * columns_, columns_};
    }

    [[nodiscard]] std::span<T> values() const noexcept {
        return {values_, rows_ * columns_};
    }

private:
    T* values_{nullptr};
    std::size_t rows_{0};
    std::size_t columns_{0};
};

struct IQMetadata {
    std::uint64_t frame_id{};
    std::uint64_t timestamp_unix_ns{};
    std::uint32_t channel_count{};
    std::uint32_t samples_per_channel{};
    double sample_rate_hz{};
    double center_frequency_hz{};
};

struct IQFrame {
    using Metadata = IQMetadata;
    using Sample = ComplexInt16;

    IQMetadata metadata{};
    Array2D<ComplexInt16> data{};
};

struct PulseCompressionMetadata {
    std::uint64_t frame_id{};
    std::uint64_t timestamp_unix_ns{};
    std::uint32_t channel_count{};
    std::uint32_t range_bin_count{};
    std::uint32_t pulse_index{};
    std::uint32_t pulses_per_cpi{};
    double range_resolution_m{};
};

struct PulseCompressionFrame {
    using Metadata = PulseCompressionMetadata;
    using Sample = ComplexFloat32;

    PulseCompressionMetadata metadata{};
    Array2D<ComplexFloat32> data{};
};

struct RDMetadata {
    std::uint64_t frame_id{};
    std::uint64_t timestamp_unix_ns{};
    std::uint32_t channel_index{};
    std::uint32_t range_bin_count{};
    std::uint32_t doppler_bin_count{};
    double range_resolution_m{};
    double velocity_resolution_mps{};
};

struct RDFrame {
    using Metadata = RDMetadata;
    using Sample = float;

    RDMetadata metadata{};
    Array2D<float> data{};
};

static_assert(std::endian::native == std::endian::little);
static_assert(std::numeric_limits<float>::is_iec559);
static_assert(std::numeric_limits<double>::is_iec559);
static_assert(std::is_trivially_copyable_v<ComplexInt16>);
static_assert(std::is_trivially_copyable_v<ComplexFloat32>);
static_assert(sizeof(ComplexInt16) == 4);
static_assert(sizeof(ComplexFloat32) == 8);
static_assert(std::is_trivially_copyable_v<IQMetadata>);
static_assert(std::is_trivially_copyable_v<PulseCompressionMetadata>);
static_assert(std::is_trivially_copyable_v<RDMetadata>);
static_assert(sizeof(IQMetadata) == 40);
static_assert(sizeof(PulseCompressionMetadata) == 40);
static_assert(sizeof(RDMetadata) == 48);

}  // namespace uestcradar
