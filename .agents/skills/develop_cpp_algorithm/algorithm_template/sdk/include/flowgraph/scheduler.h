#ifndef CYCORE_SCHEDULER_H
#define CYCORE_SCHEDULER_H

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace cy::flowgraph {

class Graph;

enum class SchedulerState {
    Idle,
    Initialised,
    Running,
    Stopping,
    Stopped,
    Failed
};

struct SchedulerOptions {
    /// 连续空闲时先执行 yield() 的次数上限，超过后切换到指数 sleep 退避。
    /// 设为 0 表示跳过 yield 阶段（纯指数 sleep 策略）。
    std::size_t idle_yield_count = 256;

    /// 指数退避的最小休眠时间（必须 > 0μs）
    std::chrono::microseconds idle_sleep_min{50};

    /// 指数退避的最大休眠时间（必须 >= idle_sleep_min）
    std::chrono::microseconds idle_sleep_max{5000};
};

/// 每个 Block 线程的运行时统计（由调度器内部采集，Block 本身零感知）
struct BlockStats {
    std::string block_name;

    std::atomic<uint64_t> work_true_count{0};       ///< work() 返回 true（有进展）的总次数
    std::atomic<uint64_t> work_false_count{0};      ///< work() 返回 false（无进展）的总次数
    std::atomic<uint64_t> consecutive_idle_peak{0}; ///< 单次连续空闲的最高峰值
    std::atomic<uint64_t> yield_count{0};           ///< 累计执行 yield() 的次数
    std::atomic<uint64_t> stall_count{0};           ///< 累计进入 sleep 退避（背压）的次数
    std::atomic<int64_t>  stall_total_ms{0};        ///< 累计背压停滞时长（ms）
    std::atomic<int64_t>  sleep_time_us{0};         ///< 累计 sleep 总时间（μs）
    std::atomic<int64_t>  max_work_latency_us{0};   ///< 单次 work()==true 的最大耗时（μs）
    std::atomic<int64_t>  total_work_latency_us{0}; ///< 所有 work()==true 的累计耗时（μs）
    std::atomic<int64_t>  thread_cpu_time_us{0};    ///< 线程 CPU 占用时间（μs, CLOCK_THREAD_CPUTIME_ID）

    BlockStats() = default;
    ~BlockStats() = default;
    BlockStats(const BlockStats&) = delete;
    BlockStats& operator=(const BlockStats&) = delete;
};

class SchedulerError : public std::runtime_error {
public:
    explicit SchedulerError(const std::string& message);
};

class ThreadPerBlockScheduler {
public:
    explicit ThreadPerBlockScheduler(Graph& graph,
                                     SchedulerOptions options = {});
    ~ThreadPerBlockScheduler();

    ThreadPerBlockScheduler(const ThreadPerBlockScheduler&) = delete;
    ThreadPerBlockScheduler& operator=(const ThreadPerBlockScheduler&) = delete;
    ThreadPerBlockScheduler(ThreadPerBlockScheduler&&) = delete;
    ThreadPerBlockScheduler& operator=(ThreadPerBlockScheduler&&) = delete;

    void init();
    void start();
    void request_stop() noexcept;
    void wait();
    void stop();

    SchedulerState state() const noexcept;
    bool failed() const noexcept;
    std::string error_message() const;

    /// 获取每个 Block 的运行时统计（索引与 Graph::blocks() 对应）
    const std::vector<std::unique_ptr<BlockStats>>& block_stats() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace cy::flowgraph

#endif // CYCORE_SCHEDULER_H
