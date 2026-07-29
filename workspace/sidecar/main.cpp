#include "ringbuf.hpp"

#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <thread>

namespace {

volatile std::sig_atomic_t running = 1;

void stop(int) {
    running = 0;
}

void install_signal_handlers() {
    std::signal(SIGINT, stop);
    std::signal(SIGTERM, stop);
}

bool write_all(RingBuffer* ring, const void* data, std::size_t len) {
    const auto* bytes = static_cast<const std::byte*>(data);
    std::size_t written = 0;
    while (running != 0 && written < len) {
        const std::int32_t result =
            ringbuf_write(ring, bytes + written, len - written);
        if (result < 0 || ringbuf_is_shutdown(ring)) {
            return false;
        }
        if (result == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        written += static_cast<std::size_t>(result);
    }
    return written == len;
}

bool read_exact(RingBuffer* ring, void* data, std::size_t len) {
    auto* bytes = static_cast<std::byte*>(data);
    std::size_t read = 0;
    while (running != 0 && read < len) {
        const std::int32_t result =
            ringbuf_read(ring, bytes + read, len - read);
        if (result < 0 || ringbuf_is_shutdown(ring)) {
            return false;
        }
        if (result == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        read += static_cast<std::size_t>(result);
    }
    return read == len;
}

int produce_upstream() {
    RingBuffer* ring = ringbuf_create(kUpstreamBufName);
    std::cout << "sidecar: created " << kUpstreamBufName << std::endl;

    std::int32_t value = 0;
    while (running != 0) {
        if (!write_all(ring, &value, sizeof(value))) {
            break;
        }
        std::cout << "upstream <- " << value << std::endl;
        ++value;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    ringbuf_shutdown(ring);
    ringbuf_close(ring);
    ringbuf_unlink(kUpstreamBufName);
    return 0;
}

int consume_downstream() {
    RingBuffer* ring = ringbuf_create(kDownstreamBufName);
    std::cout << "sidecar: created " << kDownstreamBufName << std::endl;

    while (running != 0) {
        std::int32_t value = 0;
        if (!read_exact(ring, &value, sizeof(value))) {
            break;
        }
        std::cout << "downstream -> " << value << std::endl;
    }

    ringbuf_shutdown(ring);
    ringbuf_close(ring);
    ringbuf_unlink(kDownstreamBufName);
    return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "usage: sidecar produce-upstream|consume-downstream\n";
        return 2;
    }

    install_signal_handlers();

    try {
        const std::string_view mode{argv[1]};
        if (mode == "produce-upstream") {
            return produce_upstream();
        }
        if (mode == "consume-downstream") {
            return consume_downstream();
        }
        std::cerr << "unknown mode: " << mode << '\n';
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "sidecar: " << error.what() << '\n';
        return 1;
    }
}
