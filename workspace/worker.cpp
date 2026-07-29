#include "sdk.h"

#include <cstdint>
#include <iostream>

int main() {
    if (io_open() != 0) {
        std::cerr << "worker: io_open failed\n";
        return 1;
    }

    std::cout << "worker: I/O ready" << std::endl;

    for (;;) {
        std::int32_t input = 0;
        const std::int32_t read_size = io_read(&input, sizeof(input));
        if (read_size == 0) {
            break;
        }
        if (read_size != static_cast<std::int32_t>(sizeof(input))) {
            std::cerr << "worker: io_read failed\n";
            io_close();
            return 1;
        }

        const std::int32_t output = input * 2;
        const std::int32_t write_size = io_write(&output, sizeof(output));
        if (write_size == 0) {
            break;
        }
        if (write_size != static_cast<std::int32_t>(sizeof(output))) {
            std::cerr << "worker: io_write failed\n";
            io_close();
            return 1;
        }
    }

    io_close();
    return 0;
}
