#pragma once

#include <array>
#include <cstdint>
#include <type_traits>

namespace cycore::algorithm::my_block {

inline constexpr std::size_t kMaxSamples = 1024;

struct InputData {
    std::uint32_t sample_count = 0;
    std::array<float, kMaxSamples> samples{};
};

struct OutputData {
    std::uint32_t sample_count = 0;
    std::array<float, kMaxSamples> samples{};
};

static_assert(std::is_trivially_copyable<InputData>::value,
              "InputData must be trivially copyable");
static_assert(std::is_trivially_copyable<OutputData>::value,
              "OutputData must be trivially copyable");

} // namespace cycore::algorithm::my_block
