#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace sidecar::forwarder::protocol {

inline constexpr std::uint64_t kPayloadTag = 0x4657445f44415441ULL;
inline constexpr std::uint64_t kCreditTag = 0x4657445f43524454ULL;
inline constexpr std::size_t kCreditSize = sizeof(std::uint64_t);
using CreditBytes = std::array<std::byte, kCreditSize>;

inline CreditBytes encode_credit(std::size_t value) noexcept {
    CreditBytes bytes{};
    const std::uint64_t encoded = value;
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        const unsigned shift =
            static_cast<unsigned>((bytes.size() - index - 1) * 8);
        bytes[index] =
            static_cast<std::byte>((encoded >> shift) & 0xffU);
    }
    return bytes;
}

inline std::uint64_t decode_credit(
    const CreditBytes& bytes) noexcept {
    std::uint64_t value = 0;
    for (std::byte byte : bytes) {
        value = (value << 8) |
                static_cast<std::uint64_t>(
                    std::to_integer<unsigned char>(byte));
    }
    return value;
}

}  // namespace sidecar::forwarder::protocol
