#include <sdk.h>

#include "ringbuf.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <thread>

namespace {

RingBuffer* upstream = nullptr;
RingBuffer* downstream = nullptr;

bool invalid_transfer(const void* data, std::size_t len) {
    return data == nullptr ||
           len > static_cast<std::size_t>(
                     std::numeric_limits<std::int32_t>::max());
}

}  // namespace

int32_t io_open() {
    if (upstream != nullptr && downstream != nullptr) {
        return 0;
    }

    try {
        upstream = ringbuf_open(kUpstreamBufName);
        downstream = ringbuf_open(kDownstreamBufName);
        return 0;
    } catch (...) {
        ringbuf_close(downstream);
        ringbuf_close(upstream);
        downstream = nullptr;
        upstream = nullptr;
        return -1;
    }
}

int32_t io_read(void* data, std::size_t len) {
    if (upstream == nullptr || invalid_transfer(data, len)) {
        return -1;
    }

    auto* output = static_cast<std::byte*>(data);
    std::size_t total = 0;
    while (total < len) {
        if (ringbuf_is_shutdown(upstream)) {
            break;
        }

        const std::int32_t result =
            ringbuf_read(upstream, output + total, len - total);
        if (result < 0) {
            return -1;
        }
        if (result == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        total += static_cast<std::size_t>(result);
    }
    return static_cast<std::int32_t>(total);
}

int32_t io_write(const void* data, std::size_t len) {
    if (downstream == nullptr || invalid_transfer(data, len)) {
        return -1;
    }

    const auto* input = static_cast<const std::byte*>(data);
    std::size_t total = 0;
    while (total < len) {
        if (ringbuf_is_shutdown(downstream)) {
            break;
        }

        const std::int32_t result =
            ringbuf_write(downstream, input + total, len - total);
        if (result < 0) {
            return -1;
        }
        if (result == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        total += static_cast<std::size_t>(result);
    }
    return static_cast<std::int32_t>(total);
}

void io_close() {
    ringbuf_close(downstream);
    ringbuf_close(upstream);
    downstream = nullptr;
    upstream = nullptr;
}
