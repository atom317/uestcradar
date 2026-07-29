#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

inline constexpr char kUpstreamBufName[] = "/uestcradar_upstream";
inline constexpr char kDownstreamBufName[] = "/uestcradar_downstream";
inline constexpr std::size_t kRingCapacity = 1024 * 1024;

struct alignas(64) RingBuffer {
    std::atomic<std::uint32_t> ready{0};
    std::byte ready_padding[60]{};

    alignas(64) std::atomic<std::uint64_t> write_position{0};
    std::byte write_padding[56]{};

    alignas(64) std::atomic<std::uint64_t> read_position{0};
    std::byte read_padding[56]{};

    alignas(64) std::atomic<std::uint32_t> shutdown{0};
    std::byte shutdown_padding[60]{};

    alignas(64) std::byte data[kRingCapacity]{};
};

static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);

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
