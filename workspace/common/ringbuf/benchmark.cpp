#include "ringbuf.hpp"

#include <algorithm>
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
#include <vector>
#include <unistd.h>
#include <sys/wait.h>

namespace {

struct BlockBenchmarkResult {
    std::size_t block_size{0};
    std::uint64_t iterations{0};
    double elapsed_sec{0.0};
    double throughput_gib_s{0.0};
    double ops_per_sec{0.0};
};

// 跑单个 Block Size 的吞吐基准测试
BlockBenchmarkResult run_block_benchmark(
    const std::string& name_prefix,
    std::size_t block_size,
    std::uint64_t iterations) {

    const std::string name = name_prefix + "_" + std::to_string(block_size);
    constexpr std::size_t kBenchmarkCapacity = 3 * 1024 * 1024 + 123;
    RingBuffer* ring = ringbuf_create(name.c_str(), kBenchmarkCapacity);
    if (!ring) {
        throw std::runtime_error("ringbuf_create failed for " + name);
    }
    if (ringbuf_capacity(ring) != kBenchmarkCapacity) {
        throw std::runtime_error("creator observed an invalid capacity");
    }

    const pid_t consumer_pid = ::fork();
    if (consumer_pid == -1) {
        ringbuf_shutdown(ring);
        ringbuf_close(ring);
        ringbuf_unlink(name.c_str());
        throw std::runtime_error("fork failed");
    }

    // 消费者子进程
    if (consumer_pid == 0) {
        ringbuf_close(ring);
        RingBuffer* consumer = ringbuf_open(name.c_str());
        if (ringbuf_capacity(consumer) != kBenchmarkCapacity) {
            ringbuf_close(consumer);
            ::_exit(1);
        }
        std::vector<std::byte> recv_buf(block_size);
        bool valid = true;

        for (std::uint64_t i = 0; i < iterations; ++i) {
            std::size_t read_bytes = 0;
            while (read_bytes < block_size) {
                const std::int32_t res = ringbuf_read(
                    consumer,
                    recv_buf.data() + read_bytes,
                    block_size - read_bytes);
                if (res > 0) {
                    read_bytes += static_cast<std::size_t>(res);
                } else if (res < 0 || ringbuf_is_shutdown(consumer)) {
                    valid = false;
                    break;
                } else {
                    std::this_thread::yield();
                }
            }
            if (!valid) break;
        }

        ringbuf_close(consumer);
        ::_exit(valid ? 0 : 1);
    }

    // 生产者父进程
    std::vector<std::byte> send_buf(block_size, std::byte{0xAB});
    bool producer_failed = false;

    const auto started_at = std::chrono::steady_clock::now();

    for (std::uint64_t i = 0; i < iterations; ++i) {
        std::size_t written_bytes = 0;
        while (written_bytes < block_size) {
            const std::int32_t res = ringbuf_write(
                ring,
                send_buf.data() + written_bytes,
                block_size - written_bytes);
            if (res > 0) {
                written_bytes += static_cast<std::size_t>(res);
            } else if (res < 0) {
                producer_failed = true;
                break;
            } else {
                std::this_thread::yield();
            }
        }
        if (producer_failed) break;
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
        throw std::runtime_error("data integrity / process failure during benchmark");
    }

    const double total_bytes = static_cast<double>(block_size) * static_cast<double>(iterations);
    const double total_gib = total_bytes / (1024.0 * 1024.0 * 1024.0);
    const double sec = elapsed.count();

    BlockBenchmarkResult res;
    res.block_size = block_size;
    res.iterations = iterations;
    res.elapsed_sec = sec;
    res.throughput_gib_s = total_gib / sec;
    res.ops_per_sec = static_cast<double>(iterations) / sec;
    return res;
}

}  // namespace

int main(int /*argc*/, char* /*argv*/[]) {
    try {
        const std::string name_prefix =
            "/uestcradar_bm_" + std::to_string(::getpid());

        // 逐步递增的测试块大小列表 (8B ~ 512KB)
        const std::vector<std::pair<std::size_t, std::uint64_t>> test_cases = {
            {8,        5'000'000}, // 8 B  (小消息/低延迟测试)
            {64,       2'000'000}, // 64 B (Cacheline 对齐)
            {1024,     1'000'000}, // 1 KiB
            {4096,       500'000}, // 4 KiB (标准页面大小)
            {65536,       50'000}, // 64 KiB (高吞吐小包)
            {262144,      15'000}, // 256 KiB
            {524288,       8'000}  // 512 KiB (接近 RingBuffer 容量)
        };

        std::cout << "========================================================================================\n";
        std::cout << "               RingBuffer 内存吞吐速率与 Block Size 递增基准测试 (Sweep)                \n";
        std::cout << "========================================================================================\n";
        std::cout << std::left 
                  << std::setw(12) << "Block Size" 
                  << std::setw(15) << "Iterations" 
                  << std::setw(15) << "Elapsed (s)" 
                  << std::setw(18) << "Throughput (GiB/s)" 
                  << std::setw(18) << "Throughput (MiB/s)"
                  << std::setw(15) << "Ops/sec (QPS)" 
                  << "\n";
        std::cout << "----------------------------------------------------------------------------------------\n";

        for (const auto& [block_size, iters] : test_cases) {
            auto result = run_block_benchmark(name_prefix, block_size, iters);
            
            std::string block_str;
            if (block_size < 1024) {
                block_str = std::to_string(block_size) + " B";
            } else {
                block_str = std::to_string(block_size / 1024) + " KiB";
            }

            std::cout << std::left 
                      << std::setw(12) << block_str
                      << std::setw(15) << result.iterations
                      << std::setw(15) << std::fixed << std::setprecision(4) << result.elapsed_sec
                      << std::setw(18) << std::fixed << std::setprecision(2) << result.throughput_gib_s
                      << std::setw(18) << std::fixed << std::setprecision(2) << (result.throughput_gib_s * 1024.0)
                      << std::setw(15) << std::fixed << std::setprecision(0) << result.ops_per_sec
                      << std::endl;
        }
        std::cout << "========================================================================================\n";
        return 0;

    } catch (const std::exception& error) {
        std::cerr << "benchmark failed: " << error.what() << '\n';
        return 1;
    }
}
