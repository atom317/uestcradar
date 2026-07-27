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

static_assert(
    std::is_trivially_copyable_v<InputData>,
    "Non-trivial data requires a custom FrameCodec, POD only");
static_assert(
    std::is_trivially_copyable_v<OutputData>,
    "Non-trivial data requires a custom FrameCodec, POD only");

} // namespace cycore::algorithm::my_block
