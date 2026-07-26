#pragma once

#include <cstddef>
#include <type_traits>

namespace cycore::algorithm::my_block {

using InputSample = float;
using OutputSample = float;

constexpr std::size_t kInputRows = 1;
constexpr std::size_t kInputCols = 1024;
constexpr std::size_t kOutputRows = 1;
constexpr std::size_t kOutputCols = 1024;

// The production template keeps a fixed 1 x 1024 transaction by default.
// Algorithms may interpret the same contiguous element stream with read(),
// read_matrix(), or read_cube() without changing the external Block contract.
constexpr std::size_t kInputElementsPerWork = kInputRows * kInputCols;
constexpr std::size_t kOutputElementsPerWork = kOutputRows * kOutputCols;

static_assert(std::is_trivially_copyable<InputSample>::value,
              "InputSample must be trivially copyable");
static_assert(std::is_trivially_copyable<OutputSample>::value,
              "OutputSample must be trivially copyable");

} // namespace cycore::algorithm::my_block
