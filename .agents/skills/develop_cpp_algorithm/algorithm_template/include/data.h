#pragma once

#include <cstdint>
#include <type_traits>
#include <vector>

namespace cycore::algorithm::my_block {

inline constexpr std::size_t kMaxSamples = 1024;

struct InputHeader {
    std::uint32_t sample_count = 0;
};

struct OutputHeader {
    std::uint32_t sample_count = 0;
};

struct InputData {
    InputHeader header{};
    std::vector<float> payload;
};

struct OutputData {
    OutputHeader header{};
    std::vector<float> payload;
};

static_assert(
    std::is_trivially_copyable_v<InputHeader>,
    "InputHeader must be trivially copyable");
static_assert(
    std::is_trivially_copyable_v<OutputHeader>,
    "OutputHeader must be trivially copyable");
static_assert(std::is_trivially_copyable_v<float>,
              "Payload element must be trivially copyable");

} // namespace cycore::algorithm::my_block
