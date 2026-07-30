#include "ringbuf.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <new>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

namespace {

[[noreturn]] void throw_system_error(const char* operation) {
    throw std::system_error(errno, std::generic_category(), operation);
}

std::size_t checked_mapping_size(std::size_t capacity_bytes) {
    if (capacity_bytes == 0 ||
        capacity_bytes >
            std::numeric_limits<std::size_t>::max() - kRingHeaderSize) {
        throw std::invalid_argument("ring capacity is out of range");
    }
    const std::size_t mapping_size = kRingHeaderSize + capacity_bytes;
    if (mapping_size >
        static_cast<std::size_t>(std::numeric_limits<off_t>::max())) {
        throw std::invalid_argument("ring mapping is too large");
    }
    return mapping_size;
}

void* map_ring(int fd, std::size_t mapping_size) {
    void* address = ::mmap(
        nullptr,
        mapping_size,
        PROT_READ | PROT_WRITE,
        MAP_SHARED,
        fd,
        0);
    if (address == MAP_FAILED) {
        throw_system_error("mmap");
    }
    return address;
}

RingBuffer* make_handle(void* address, std::size_t mapping_size) {
    try {
        return new RingBuffer{
            static_cast<RingBufferHeader*>(address),
            static_cast<std::byte*>(address) + kRingHeaderSize,
            mapping_size,
        };
    } catch (...) {
        ::munmap(address, mapping_size);
        throw;
    }
}

}  // namespace

RingBuffer* ringbuf_create(
    const char* name,
    std::size_t capacity_bytes) {
    if (name == nullptr || name[0] == '\0') {
        throw std::invalid_argument("ring name must not be empty");
    }
    const std::size_t mapping_size =
        checked_mapping_size(capacity_bytes);

    if (::shm_unlink(name) == -1 && errno != ENOENT) {
        throw_system_error("shm_unlink");
    }

    const int fd = ::shm_open(name, O_CREAT | O_EXCL | O_RDWR, 0666);
    if (fd == -1) {
        throw_system_error("shm_open(create)");
    }

    if (::ftruncate(fd, static_cast<off_t>(mapping_size)) == -1) {
        const int saved_errno = errno;
        ::close(fd);
        ::shm_unlink(name);
        errno = saved_errno;
        throw_system_error("ftruncate");
    }

    void* address = nullptr;
    try {
        address = map_ring(fd, mapping_size);
    } catch (...) {
        ::close(fd);
        ::shm_unlink(name);
        throw;
    }
    ::close(fd);

    auto* header = ::new (address) RingBufferHeader{};
    header->capacity_bytes = capacity_bytes;
    header->magic.store(kRingMagic, std::memory_order_release);
    return make_handle(address, mapping_size);
}

RingBuffer* ringbuf_open(const char* name) {
    if (name == nullptr || name[0] == '\0') {
        throw std::invalid_argument("ring name must not be empty");
    }

    for (;;) {
        const int fd = ::shm_open(name, O_RDWR, 0);
        if (fd == -1) {
            if (errno == ENOENT) {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(10));
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
        if (status.st_size < static_cast<off_t>(kRingHeaderSize) ||
            static_cast<std::uintmax_t>(status.st_size) >
                std::numeric_limits<std::size_t>::max()) {
            ::close(fd);
            std::this_thread::sleep_for(
                std::chrono::milliseconds(10));
            continue;
        }

        const std::size_t mapping_size =
            static_cast<std::size_t>(status.st_size);
        void* address = nullptr;
        try {
            address = map_ring(fd, mapping_size);
        } catch (...) {
            ::close(fd);
            throw;
        }
        ::close(fd);

        auto* header = static_cast<RingBufferHeader*>(address);
        while (header->magic.load(std::memory_order_acquire) !=
               kRingMagic) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(1));
        }

        bool valid = header->abi_version == kRingAbiVersion &&
                     header->header_size == kRingHeaderSize &&
                     header->capacity_bytes > 0;
        if (valid) {
            valid = header->capacity_bytes ==
                    mapping_size - kRingHeaderSize;
        }
        if (!valid) {
            ::munmap(address, mapping_size);
            errno = EPROTO;
            throw_system_error("ringbuf ABI");
        }
        return make_handle(address, mapping_size);
    }
}

std::int32_t ringbuf_write(
    RingBuffer* ring,
    const void* data,
    std::size_t len) {
    if (ring == nullptr || ring->header == nullptr ||
        data == nullptr ||
        len > static_cast<std::size_t>(
                  std::numeric_limits<std::int32_t>::max())) {
        return -1;
    }
    if (len == 0 || ringbuf_is_shutdown(ring)) {
        return 0;
    }

    const std::size_t capacity = ringbuf_capacity(ring);
    const std::uint64_t write =
        ring->header->write_position.load(std::memory_order_relaxed);
    const std::uint64_t read =
        ring->header->read_position.load(std::memory_order_acquire);
    if (write < read || write - read > capacity) {
        return -1;
    }

    const std::size_t available =
        capacity - static_cast<std::size_t>(write - read);
    const std::size_t write_size = std::min(len, available);
    if (write_size == 0) {
        return 0;
    }

    const auto* source = static_cast<const std::byte*>(data);
    const std::size_t position = write % capacity;
    const std::size_t first_size =
        std::min(write_size, capacity - position);
    std::memcpy(ring->data + position, source, first_size);
    std::memcpy(
        ring->data,
        source + first_size,
        write_size - first_size);

    ring->header->write_position.store(
        write + write_size,
        std::memory_order_release);
    return static_cast<std::int32_t>(write_size);
}

std::int32_t ringbuf_read(
    RingBuffer* ring,
    void* data,
    std::size_t len) {
    if (ring == nullptr || ring->header == nullptr ||
        data == nullptr ||
        len > static_cast<std::size_t>(
                  std::numeric_limits<std::int32_t>::max())) {
        return -1;
    }
    if (len == 0) {
        return 0;
    }

    const std::size_t capacity = ringbuf_capacity(ring);
    const std::uint64_t read =
        ring->header->read_position.load(std::memory_order_relaxed);
    const std::uint64_t write =
        ring->header->write_position.load(std::memory_order_acquire);
    if (write < read || write - read > capacity) {
        return -1;
    }
    const std::size_t read_size =
        std::min(len, static_cast<std::size_t>(write - read));
    if (read_size == 0) {
        return 0;
    }

    const std::size_t position = read % capacity;
    const std::size_t first_size =
        std::min(read_size, capacity - position);
    std::memcpy(data, ring->data + position, first_size);
    std::memcpy(
        static_cast<std::byte*>(data) + first_size,
        ring->data,
        read_size - first_size);

    ring->header->read_position.store(
        read + read_size,
        std::memory_order_release);
    return static_cast<std::int32_t>(read_size);
}

std::span<const std::byte> ringbuf_peek_read(
    RingBuffer* ring) noexcept {
    if (ring == nullptr || ring->header == nullptr ||
        ring->data == nullptr) {
        return {};
    }

    const std::size_t capacity = ringbuf_capacity(ring);
    const std::uint64_t read =
        ring->header->read_position.load(std::memory_order_relaxed);
    const std::uint64_t write =
        ring->header->write_position.load(std::memory_order_acquire);
    if (capacity == 0 || write < read || write - read > capacity) {
        return {};
    }

    const std::size_t available =
        static_cast<std::size_t>(write - read);
    const std::size_t position =
        static_cast<std::size_t>(read % capacity);
    const std::size_t contiguous =
        std::min(available, capacity - position);
    return {ring->data + position, contiguous};
}

bool ringbuf_commit_read(
    RingBuffer* ring,
    std::size_t len) noexcept {
    if (ring == nullptr || ring->header == nullptr) {
        return false;
    }
    if (len == 0) {
        return true;
    }

    const std::size_t capacity = ringbuf_capacity(ring);
    const std::uint64_t read =
        ring->header->read_position.load(std::memory_order_relaxed);
    const std::uint64_t write =
        ring->header->write_position.load(std::memory_order_acquire);
    if (capacity == 0 || write < read || write - read > capacity) {
        return false;
    }
    const std::size_t position =
        static_cast<std::size_t>(read % capacity);
    const std::size_t contiguous = std::min(
        static_cast<std::size_t>(write - read),
        capacity - position);
    if (len > contiguous) {
        return false;
    }

    ring->header->read_position.store(
        read + len,
        std::memory_order_release);
    return true;
}

std::span<std::byte> ringbuf_reserve_write(
    RingBuffer* ring) noexcept {
    if (ring == nullptr || ring->header == nullptr ||
        ring->data == nullptr || ringbuf_is_shutdown(ring)) {
        return {};
    }

    const std::size_t capacity = ringbuf_capacity(ring);
    const std::uint64_t write =
        ring->header->write_position.load(std::memory_order_relaxed);
    const std::uint64_t read =
        ring->header->read_position.load(std::memory_order_acquire);
    if (capacity == 0 || write < read || write - read > capacity) {
        return {};
    }

    const std::size_t free =
        capacity - static_cast<std::size_t>(write - read);
    const std::size_t position =
        static_cast<std::size_t>(write % capacity);
    const std::size_t contiguous =
        std::min(free, capacity - position);
    return {ring->data + position, contiguous};
}

bool ringbuf_commit_write(
    RingBuffer* ring,
    std::size_t len) noexcept {
    if (ring == nullptr || ring->header == nullptr ||
        ringbuf_is_shutdown(ring)) {
        return false;
    }
    if (len == 0) {
        return true;
    }

    const std::size_t capacity = ringbuf_capacity(ring);
    const std::uint64_t write =
        ring->header->write_position.load(std::memory_order_relaxed);
    const std::uint64_t read =
        ring->header->read_position.load(std::memory_order_acquire);
    if (capacity == 0 || write < read || write - read > capacity) {
        return false;
    }
    const std::size_t position =
        static_cast<std::size_t>(write % capacity);
    const std::size_t contiguous = std::min(
        capacity - static_cast<std::size_t>(write - read),
        capacity - position);
    if (len > contiguous) {
        return false;
    }

    ring->header->write_position.store(
        write + len,
        std::memory_order_release);
    return true;
}

std::size_t ringbuf_capacity(const RingBuffer* ring) noexcept {
    return ring == nullptr || ring->header == nullptr
               ? 0
               : static_cast<std::size_t>(
                     ring->header->capacity_bytes);
}

bool ringbuf_is_shutdown(const RingBuffer* ring) noexcept {
    return ring != nullptr && ring->header != nullptr &&
           ring->header->shutdown.load(std::memory_order_acquire) != 0;
}

void ringbuf_shutdown(RingBuffer* ring) noexcept {
    if (ring != nullptr && ring->header != nullptr) {
        ring->header->shutdown.store(1, std::memory_order_release);
    }
}

void ringbuf_close(RingBuffer* ring) noexcept {
    if (ring != nullptr) {
        if (ring->header != nullptr && ring->mapping_size != 0) {
            ::munmap(ring->header, ring->mapping_size);
        }
        delete ring;
    }
}

void ringbuf_unlink(const char* name) noexcept {
    if (name != nullptr) {
        ::shm_unlink(name);
    }
}
