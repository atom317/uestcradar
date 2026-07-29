#pragma once

#include <common/data_types.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

namespace cycore::algorithm::pulse_compression {

inline constexpr std::uint32_t kDefaultPoints = 1024;
inline constexpr std::uint32_t kReplicaPoints = 256;

struct PulseCompressionHeader {
    std::uint32_t points = 0;
};

struct InputData {
    PulseCompressionHeader header{};
    std::vector<cy::common::CS16> payload;
};

struct OutputData {
    PulseCompressionHeader header{};
    std::vector<cy::common::CS16> payload;
};

static_assert(std::is_trivially_copyable_v<PulseCompressionHeader>,
              "PulseCompressionHeader must be trivially copyable");
static_assert(std::is_trivially_copyable_v<cy::common::CS16>,
              "Pulse-compression samples must be trivially copyable");

} // namespace cycore::algorithm::pulse_compression
