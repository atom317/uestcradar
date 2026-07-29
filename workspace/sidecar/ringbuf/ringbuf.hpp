#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

inline constexpr char kUpstreamBufName[] = "/uestcradar_upstream";
inline constexpr char kDownstreamBufName[] = "/uestcradar_downstream";
inline constexpr std::uint32_t kRingMagic = 0x52494E47;
inline constexpr std::uint16_t kRingAbiVersion = 3;
inline constexpr std::size_t kRingHeaderSize = 4096;
inline constexpr std::size_t kRingCapacity = 1024 * 1024;

// This shared-memory ring is single-producer/single-consumer.  The producer
// exclusively updates write_position and the consumer exclusively updates
// read_position, so no mutex or compare-exchange loop is required.
struct alignas(kRingHeaderSize) RingBufferHeader {
    std::atomic<std::uint32_t> magic{0};
    std::uint16_t abi_version{kRingAbiVersion};
    std::uint16_t header_size{kRingHeaderSize};
    std::uint64_t capacity_bytes{kRingCapacity};
    std::byte metadata_padding[48]{};

    alignas(64) std::atomic<std::uint64_t> write_position{0};
    std::byte write_padding[56]{};

    alignas(64) std::atomic<std::uint64_t> read_position{0};
    std::byte read_padding[56]{};

    alignas(64) std::atomic<std::uint32_t> shutdown{0};
    std::byte shutdown_padding[60]{};

    std::byte reserved[kRingHeaderSize - 256]{};
};

struct RingBuffer {
    RingBufferHeader header;
    alignas(kRingHeaderSize) std::byte data[kRingCapacity]{};
};

static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
static_assert(sizeof(RingBufferHeader) == kRingHeaderSize);
static_assert(offsetof(RingBufferHeader, write_position) == 64);
static_assert(offsetof(RingBufferHeader, read_position) == 128);
static_assert(offsetof(RingBufferHeader, shutdown) == 192);
static_assert(offsetof(RingBuffer, data) == kRingHeaderSize);

[[nodiscard]] RingBuffer* ringbuf_create(const char* name);
[[nodiscard]] RingBuffer* ringbuf_open(const char* name);

[[nodiscard]] std::int32_t ringbuf_write(
    RingBuffer* ring,
    const void* data,
    std::size_t len);
[[nodiscard]] std::int32_t ringbuf_read(
    RingBuffer* ring,
    void* data,
    std::size_t len);

[[nodiscard]] bool ringbuf_is_shutdown(const RingBuffer* ring) noexcept;
void ringbuf_shutdown(RingBuffer* ring) noexcept;
void ringbuf_close(RingBuffer* ring) noexcept;
void ringbuf_unlink(const char* name) noexcept;
