#include "ringbuf.hpp"

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <stdexcept>
#include <thread>
#include <unistd.h>
#include <sys/wait.h>

namespace {

constexpr std::uint64_t kDefaultIterations = 10'000'000;

std::uint64_t parse_iterations(int argc, char* argv[]) {
    if (argc == 1) {
        return kDefaultIterations;
    }
    if (argc != 2) {
        throw std::runtime_error("usage: ringbuf-benchmark [iterations]");
    }

    char* end = nullptr;
    errno = 0;
    const unsigned long long parsed = std::strtoull(argv[1], &end, 10);
    if (errno != 0 || end == argv[1] || *end != '\0' || parsed == 0 ||
        parsed > std::numeric_limits<std::uint64_t>::max()) {
        throw std::runtime_error("iterations must be a positive uint64");
    }
    return static_cast<std::uint64_t>(parsed);
}

}  // namespace

int main(int argc, char* argv[]) {
    try {
        const std::uint64_t iterations = parse_iterations(argc, argv);
        const std::string name =
            "/uestcradar_ringbuf_benchmark_" + std::to_string(::getpid());
        RingBuffer* ring = ringbuf_create(name.c_str());
        const pid_t consumer_pid = ::fork();
        if (consumer_pid == -1) {
            throw std::runtime_error("fork failed");
        }
        if (consumer_pid == 0) {
            ringbuf_close(ring);
            RingBuffer* consumer = ringbuf_open(name.c_str());
            bool valid = true;
            for (std::uint64_t expected = 0; expected < iterations;) {
                std::uint64_t value = 0;
                const std::int32_t result =
                    ringbuf_read(consumer, &value, sizeof(value));
                if (result == static_cast<std::int32_t>(sizeof(value))) {
                    valid = value == expected;
                    ++expected;
                } else if (result < 0 || ringbuf_is_shutdown(consumer)) {
                    valid = false;
                    break;
                } else {
                    std::this_thread::yield();
                }
                if (!valid) {
                    break;
                }
            }
            ringbuf_close(consumer);
            ::_exit(valid ? 0 : 1);
        }

        bool producer_failed = false;
        const auto started_at = std::chrono::steady_clock::now();
        for (std::uint64_t value = 0; value < iterations;) {
            const std::int32_t result = ringbuf_write(ring, &value, sizeof(value));
            if (result == static_cast<std::int32_t>(sizeof(value))) {
                ++value;
            } else if (result < 0) {
                producer_failed = true;
                break;
            } else {
                std::this_thread::yield();
            }
        }
        if (producer_failed) {
            ringbuf_shutdown(ring);
        }
        int consumer_status = 0;
        while (::waitpid(consumer_pid, &consumer_status, 0) == -1) {
            if (errno != EINTR) {
                throw std::runtime_error("waitpid failed");
            }
        }
        const auto elapsed = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - started_at);

        ringbuf_shutdown(ring);
        ringbuf_close(ring);
        ringbuf_unlink(name.c_str());

        if (producer_failed || !WIFEXITED(consumer_status) ||
            WEXITSTATUS(consumer_status) != 0) {
            std::cerr << "ringbuf benchmark: data integrity failure\n";
            return 1;
        }

        const double seconds = elapsed.count();
        const double mib = static_cast<double>(iterations * sizeof(std::uint64_t)) /
                           (1024.0 * 1024.0);
        std::cout << std::fixed << std::setprecision(2)
                  << "iterations=" << iterations
                  << " throughput_mib_s=" << mib / seconds
                  << " ns_per_item="
                  << seconds * 1'000'000'000.0 / static_cast<double>(iterations)
                  << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ringbuf benchmark: " << error.what() << '\n';
        return 1;
    }
}
