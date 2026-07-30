#include "sdk.h"

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>

namespace {

std::size_t worker_block_size() {
    constexpr std::size_t fallback = 256 * 1024;
    const char* value = std::getenv("WORKER_BLOCK_BYTES");
    if (value == nullptr || value[0] == '\0') {
        return fallback;
    }
    char* end = nullptr;
    errno = 0;
    const unsigned long long parsed =
        std::strtoull(value, &end, 10);
    if (errno != 0 || end == value || *end != '\0' ||
        parsed == 0 ||
        parsed >
            static_cast<unsigned long long>(
                std::numeric_limits<std::int32_t>::max())) {
        return fallback;
    }
    return static_cast<std::size_t>(parsed);
}

}  // namespace

int main() {
    if (io_open() != 0) {
        std::cerr << "worker: io_open failed\n";
        return 1;
    }

    std::vector<std::byte> block(worker_block_size());
    std::cout
        << "worker: byte-stream echo ready, block="
        << block.size() << " bytes" << std::endl;

    for (;;) {
        const std::int32_t read_size =
            io_read(block.data(), block.size());
        if (read_size == 0) {
            break;
        }
        if (read_size !=
            static_cast<std::int32_t>(block.size())) {
            std::cerr << "worker: io_read failed\n";
            io_close();
            return 1;
        }

        const std::int32_t write_size =
            io_write(block.data(), block.size());
        if (write_size == 0) {
            break;
        }
        if (write_size !=
            static_cast<std::int32_t>(block.size())) {
            std::cerr << "worker: io_write failed\n";
            io_close();
            return 1;
        }
    }

    io_close();
    return 0;
}
