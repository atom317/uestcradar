#pragma once

#include "data.h"

#include <cycore_algorithm_sdk.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>

namespace cycore::algorithm::my_block::detail {

inline std::uint32_t read_u32_le(const std::byte* bytes) noexcept {
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < sizeof(value); ++i) {
        value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[i]))
                 << (i * 8U);
    }
    return value;
}

inline void write_u32_le(std::byte* bytes, std::uint32_t value) noexcept {
    for (std::size_t i = 0; i < sizeof(value); ++i) {
        bytes[i] = static_cast<std::byte>((value >> (i * 8U)) & 0xffU);
    }
}

template <typename Data>
std::size_t encoded_size(const Data& value) {
    if (value.sample_count == 0 || value.sample_count > kMaxSamples) {
        throw std::length_error("sample_count is outside the payload contract");
    }
    return sizeof(std::uint32_t) +
           static_cast<std::size_t>(value.sample_count) * sizeof(float);
}

template <typename Data>
bool decode(cy::common::Span<const std::byte> payload, Data& value) noexcept {
    if (payload.size() < sizeof(std::uint32_t)) {
        return false;
    }
    const std::uint32_t count = read_u32_le(payload.data());
    if (count == 0 || count > kMaxSamples) {
        return false;
    }
    const std::size_t expected =
        sizeof(std::uint32_t) + static_cast<std::size_t>(count) * sizeof(float);
    if (payload.size() != expected) {
        return false;
    }
    value.sample_count = count;
    std::memcpy(value.samples.data(),
                payload.data() + sizeof(std::uint32_t),
                static_cast<std::size_t>(count) * sizeof(float));
    return true;
}

template <typename Data>
bool encode(const Data& value, cy::common::Span<std::byte> payload) noexcept {
    if (value.sample_count == 0 || value.sample_count > kMaxSamples) {
        return false;
    }
    const std::size_t expected =
        sizeof(std::uint32_t) +
        static_cast<std::size_t>(value.sample_count) * sizeof(float);
    if (payload.size() != expected) {
        return false;
    }
    write_u32_le(payload.data(), value.sample_count);
    std::memcpy(payload.data() + sizeof(std::uint32_t),
                value.samples.data(),
                static_cast<std::size_t>(value.sample_count) * sizeof(float));
    return true;
}

} // namespace cycore::algorithm::my_block::detail

namespace cycore::sdk {

template <>
struct FrameCodec<cycore::algorithm::my_block::InputData> {
    using Data = cycore::algorithm::my_block::InputData;

    static std::size_t encoded_size(const Data& value) {
        return cycore::algorithm::my_block::detail::encoded_size(value);
    }
    static bool decode(cy::common::Span<const std::byte> payload,
                       Data& value) noexcept {
        return cycore::algorithm::my_block::detail::decode(payload, value);
    }
    static bool encode(const Data& value,
                       cy::common::Span<std::byte> payload) noexcept {
        return cycore::algorithm::my_block::detail::encode(value, payload);
    }
};

template <>
struct FrameCodec<cycore::algorithm::my_block::OutputData> {
    using Data = cycore::algorithm::my_block::OutputData;

    static std::size_t encoded_size(const Data& value) {
        return cycore::algorithm::my_block::detail::encoded_size(value);
    }
    static bool decode(cy::common::Span<const std::byte> payload,
                       Data& value) noexcept {
        return cycore::algorithm::my_block::detail::decode(payload, value);
    }
    static bool encode(const Data& value,
                       cy::common::Span<std::byte> payload) noexcept {
        return cycore::algorithm::my_block::detail::encode(value, payload);
    }
};

} // namespace cycore::sdk
