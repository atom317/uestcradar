#pragma once

#include <common/data_types.h>

#include <cstdint>
#include <type_traits>
#include <vector>

namespace uestcradar::nodes {

struct IQFrameHeader {
    std::uint32_t points = 0;
};

struct IQFrame {
    IQFrameHeader header{};
    std::vector<cy::common::CS16> payload;
};

static_assert(sizeof(IQFrameHeader) == 4, "IQFrameHeader wire size changed");
static_assert(sizeof(cy::common::CS16) == 4, "CS16 wire size changed");
static_assert(std::is_trivially_copyable_v<IQFrameHeader>);
static_assert(std::is_trivially_copyable_v<cy::common::CS16>);

} // namespace uestcradar::nodes
