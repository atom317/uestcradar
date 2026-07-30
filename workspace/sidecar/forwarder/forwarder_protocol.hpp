#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace sidecar::forwarder::protocol {

inline constexpr std::uint64_t kPayloadTag = 0x4657445f44415441ULL;
inline constexpr std::uint64_t kCreditTag = 0x4657445f43524454ULL;
inline constexpr std::uint64_t kHelloTag = 0x4657445f48454c4fULL;
inline constexpr std::uint32_t kProtocolVersion = 1;

using CreditBytes = std::array<std::byte, sizeof(std::uint64_t)>;
using HelloBytes = std::array<std::byte, 40>;

struct PortContract {
    std::uint64_t type_id;
    std::uint32_t type_version;
    std::uint32_t max_payload_bytes;
};

struct Hello {
    PortContract outbound;
    PortContract inbound;
};

template <class Bytes>
inline void encode_unsigned(
    Bytes& bytes,
    std::size_t offset,
    std::uint64_t value,
    std::size_t width) noexcept {
    for (std::size_t index = 0; index < width; ++index) {
        const unsigned shift =
            static_cast<unsigned>((width - index - 1) * 8);
        bytes[offset + index] =
            static_cast<std::byte>((value >> shift) & 0xffU);
    }
}

template <class Bytes>
inline std::uint64_t decode_unsigned(
    const Bytes& bytes,
    std::size_t offset,
    std::size_t width) noexcept {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < width; ++index) {
        value = (value << 8) |
                std::to_integer<unsigned char>(bytes[offset + index]);
    }
    return value;
}

inline CreditBytes encode_credit(std::size_t value) noexcept {
    CreditBytes bytes{};
    encode_unsigned(bytes, 0, value, bytes.size());
    return bytes;
}

inline std::uint64_t decode_credit(
    const CreditBytes& bytes) noexcept {
    return decode_unsigned(bytes, 0, bytes.size());
}

inline HelloBytes encode_hello(const Hello& hello) noexcept {
    HelloBytes bytes{};
    encode_unsigned(bytes, 0, kProtocolVersion, 4);
    encode_unsigned(bytes, 8, hello.outbound.type_id, 8);
    encode_unsigned(bytes, 16, hello.outbound.type_version, 4);
    encode_unsigned(bytes, 20, hello.outbound.max_payload_bytes, 4);
    encode_unsigned(bytes, 24, hello.inbound.type_id, 8);
    encode_unsigned(bytes, 32, hello.inbound.type_version, 4);
    encode_unsigned(bytes, 36, hello.inbound.max_payload_bytes, 4);
    return bytes;
}

inline bool decode_hello(
    const HelloBytes& bytes,
    Hello& hello) noexcept {
    if (decode_unsigned(bytes, 0, 4) != kProtocolVersion ||
        decode_unsigned(bytes, 4, 4) != 0) {
        return false;
    }
    hello = {
        {
            decode_unsigned(bytes, 8, 8),
            static_cast<std::uint32_t>(
                decode_unsigned(bytes, 16, 4)),
            static_cast<std::uint32_t>(
                decode_unsigned(bytes, 20, 4)),
        },
        {
            decode_unsigned(bytes, 24, 8),
            static_cast<std::uint32_t>(
                decode_unsigned(bytes, 32, 4)),
            static_cast<std::uint32_t>(
                decode_unsigned(bytes, 36, 4)),
        },
    };
    return hello.outbound.type_id != 0 &&
           hello.outbound.type_version != 0 &&
           hello.outbound.max_payload_bytes != 0 &&
           hello.inbound.type_id != 0 &&
           hello.inbound.type_version != 0 &&
           hello.inbound.max_payload_bytes != 0;
}

}  // namespace sidecar::forwarder::protocol
