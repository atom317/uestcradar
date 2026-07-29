#include "ringbuf.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <new>
#include <system_error>
#include <thread>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

namespace {

[[noreturn]] void throw_system_error(const char* operation) {
    throw std::system_error(errno, std::generic_category(), operation);
}

RingBuffer* map_ring(int fd) {
    void* address =
        ::mmap(nullptr, sizeof(RingBuffer), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (address == MAP_FAILED) {
        throw_system_error("mmap");
    }
    return static_cast<RingBuffer*>(address);
}

}  // namespace

RingBuffer* ringbuf_create(const char* name) {
    if (::shm_unlink(name) == -1 && errno != ENOENT) {
        throw_system_error("shm_unlink");
    }

    const int fd = ::shm_open(name, O_CREAT | O_EXCL | O_RDWR, 0666);
    if (fd == -1) {
        throw_system_error("shm_open(create)");
    }

    if (::ftruncate(fd, sizeof(RingBuffer)) == -1) {
        const int saved_errno = errno;
        ::close(fd);
        ::shm_unlink(name);
        errno = saved_errno;
        throw_system_error("ftruncate");
    }

    RingBuffer* ring = nullptr;
    try {
        ring = map_ring(fd);
    } catch (...) {
        ::close(fd);
        ::shm_unlink(name);
        throw;
    }
    ::close(fd);

    new (ring) RingBuffer();
    ring->header.magic.store(kRingMagic, std::memory_order_release);
    return ring;
}

RingBuffer* ringbuf_open(const char* name) {
    for (;;) {
        const int fd = ::shm_open(name, O_RDWR, 0);
        if (fd == -1) {
            if (errno == ENOENT) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            throw_system_error("shm_open(open)");
        }

        struct stat status {};
        if (::fstat(fd, &status) == -1) {
            const int saved_errno = errno;
            ::close(fd);
            errno = saved_errno;
            throw_system_error("fstat");
        }
        if (status.st_size != static_cast<off_t>(sizeof(RingBuffer))) {
            ::close(fd);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        RingBuffer* ring = map_ring(fd);
        ::close(fd);
        while (ring->header.magic.load(std::memory_order_acquire) != kRingMagic) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (ring->header.abi_version != kRingAbiVersion ||
            ring->header.header_size != kRingHeaderSize ||
            ring->header.capacity_bytes != kRingCapacity) {
            ringbuf_close(ring);
            errno = EPROTO;
            throw_system_error("ringbuf ABI");
        }
        return ring;
    }
}

std::int32_t ringbuf_write(
    RingBuffer* ring,
    const void* data,
    std::size_t len) {
    if (ring == nullptr || data == nullptr ||
        len > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
        return -1;
    }
    if (len == 0 || ringbuf_is_shutdown(ring)) {
        return 0;
    }

    const std::uint64_t write =
        ring->header.write_position.load(std::memory_order_relaxed);
    const std::uint64_t read =
        ring->header.read_position.load(std::memory_order_acquire);
    const std::size_t free_space = kRingCapacity - (write - read);
    const std::size_t write_size = std::min(len, free_space);
    if (write_size == 0) {
        return 0;
    }

    const std::size_t position = write % kRingCapacity;
    const std::size_t first_size =
        std::min(write_size, kRingCapacity - position);
    std::memcpy(ring->data + position, data, first_size);
    std::memcpy(
        ring->data,
        static_cast<const std::byte*>(data) + first_size,
        write_size - first_size);

    ring->header.write_position.store(write + write_size, std::memory_order_release);
    return static_cast<std::int32_t>(write_size);
}

std::int32_t ringbuf_read(
    RingBuffer* ring,
    void* data,
    std::size_t len) {
    if (ring == nullptr || data == nullptr ||
        len > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
        return -1;
    }
    if (len == 0) {
        return 0;
    }

    const std::uint64_t read =
        ring->header.read_position.load(std::memory_order_relaxed);
    const std::uint64_t write =
        ring->header.write_position.load(std::memory_order_acquire);
    const std::size_t available = write - read;
    const std::size_t read_size = std::min(len, available);
    if (read_size == 0) {
        return 0;
    }

    const std::size_t position = read % kRingCapacity;
    const std::size_t first_size =
        std::min(read_size, kRingCapacity - position);
    std::memcpy(data, ring->data + position, first_size);
    std::memcpy(
        static_cast<std::byte*>(data) + first_size,
        ring->data,
        read_size - first_size);

    ring->header.read_position.store(read + read_size, std::memory_order_release);
    return static_cast<std::int32_t>(read_size);
}

bool ringbuf_is_shutdown(const RingBuffer* ring) noexcept {
    return ring != nullptr &&
           ring->header.shutdown.load(std::memory_order_acquire) != 0;
}

void ringbuf_shutdown(RingBuffer* ring) noexcept {
    if (ring != nullptr) {
        ring->header.shutdown.store(1, std::memory_order_release);
    }
}

void ringbuf_close(RingBuffer* ring) noexcept {
    if (ring != nullptr) {
        ::munmap(ring, sizeof(RingBuffer));
    }
}

void ringbuf_unlink(const char* name) noexcept {
    ::shm_unlink(name);
}
