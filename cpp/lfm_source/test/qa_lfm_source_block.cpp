#include <flowgraph/blocks/common/lfm_source.h>
#include <flowgraph/blocks/io/device_sink_block.h>
#include <common/i_data_stream.h>
#include <common/span.h>
#include <chrono>
#include <cstddef>
#include <cmath>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace fg = cy::flowgraph;

namespace {

class CaptureWriter final : public cy::common::IDataWriter {
public:
    explicit CaptureWriter(std::size_t short_by = 0) : short_by_(short_by) {}

    bool is_active() const override { return true; }
    cy::common::DataType get_data_type() const override {
        return cy::common::DataType::CS16;
    }
    std::size_t get_element_size() const override {
        return sizeof(cy::common::CS16);
    }
    int write(cy::common::Span<const std::byte> buffer, long) override {
        ++calls;
        requested_bytes = buffer.size();
        bytes.assign(buffer.begin(), buffer.end());
        return static_cast<int>(buffer.size() - std::min(short_by_, buffer.size()));
    }

    std::vector<std::byte> bytes;
    std::size_t calls = 0;
    std::size_t requested_bytes = 0;

private:
    std::size_t short_by_ = 0;
};

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void test_configurable_two_channel_layout() {
    fg::ValueMap params;
    params["num_channels"] = std::int64_t{2};
    params["samples_per_pulse"] = std::int64_t{512};
    params["batch_size"] = std::int64_t{256};

    cy::flowgraph::blocks::common::LFMSource source(params);
    fg::PortIn<cy::common::CS16> sink;
    fg::connect(source.out, sink, 512);
    source.process_work();

    auto output = sink.get(256);
    require(output.size() == 256, "Two-channel source must publish one complete batch");
    require(source.total_elements() == 2 * 512,
            "Two-channel total element count mismatch");

    for (std::size_t i = 0; i < 200; ++i) {
        require(output[i] == cy::common::CS16{0, 0},
                "Two-channel delay interval must remain interleaved and zero");
    }
    require(output[200] != cy::common::CS16{0, 0},
            "Two-channel ch0 active sample missing");
    require(output[201] != cy::common::CS16{0, 0},
            "Two-channel ch1 active sample missing");
    output.consume(output.size());
}

void test_invalid_batch_shape() {
    fg::ValueMap params;
    params["num_channels"] = std::int64_t{8};
    params["batch_size"] = std::int64_t{127};
    bool threw = false;
    try {
        (void)cy::flowgraph::blocks::common::LFMSource(params);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw, "LFMSource must reject batches that split a channel group");
}

void test_invalid_diagnostic_params() {
    {
        fg::ValueMap params;
        params["waveform_type"] = std::string{"unknown"};
        bool threw = false;
        try {
            (void)cy::flowgraph::blocks::common::LFMSource(params);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        require(threw, "LFMSource must reject an unknown waveform_type");
    }
    {
        fg::ValueMap params;
        params["amplitude"] = 8192.0;
        bool threw = false;
        try {
            (void)cy::flowgraph::blocks::common::LFMSource(params);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        require(threw, "LFMSource must reject amplitude above 8191");
    }
    {
        fg::ValueMap params;
        params["waveform_type"] = std::string{"tone"};
        params["num_channels"] = std::int64_t{8};
        params["batch_size"] = std::int64_t{32768};
        params["sample_rate"] = 30.72e6;
        params["tone_frequency"] = 1.0e6;
        bool threw = false;
        try {
            (void)cy::flowgraph::blocks::common::LFMSource(params);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        require(threw, "LFMSource must reject a non-coherent tone batch");
    }
}

void test_coherent_tone_batch_boundary(std::size_t samples_per_channel) {
    constexpr std::size_t channels = 2;
    const std::size_t batch_elements = samples_per_channel * channels;
    fg::ValueMap params;
    params["waveform_type"] = std::string{"tone"};
    params["num_channels"] = std::int64_t{channels};
    params["batch_size"] = static_cast<std::int64_t>(batch_elements);
    params["sample_rate"] = 30.72e6;
    params["tone_frequency"] = 960.0e3;
    params["amplitude"] = 4096.0;
    params["channel_phase_step"] = 0.0;

    cy::flowgraph::blocks::common::LFMSource source(params);
    fg::PortIn<cy::common::CS16> sink;
    fg::connect(source.out, sink, batch_elements * 2);
    source.process_work();
    source.process_work();

    auto output = sink.get(batch_elements * 2);
    require(output.size() == batch_elements * 2,
            "Tone source must commit two complete batches");
    constexpr double phase_step = 2.0 * 3.14159265358979323846 / 32.0;
    for (std::size_t sample = 0; sample < samples_per_channel * 2; ++sample) {
        const auto expected_i = static_cast<std::int16_t>(
            std::lround(4096.0 * std::cos(phase_step * static_cast<double>(sample))));
        const auto expected_q = static_cast<std::int16_t>(
            std::lround(4096.0 * std::sin(phase_step * static_cast<double>(sample))));
        for (std::size_t channel = 0; channel < channels; ++channel) {
            const auto& value = output[sample * channels + channel];
            require(value.i == expected_i && value.q == expected_q,
                    "Tone sample mismatch across batch boundary");
        }
    }
    for (std::size_t i = 0; i < batch_elements; ++i) {
        require(output[i] == output[batch_elements + i],
                "Coherent tone batch must repeat without a boundary discontinuity");
    }
    output.consume(output.size());
}

void test_constant_channel_iq_layout() {
    fg::ValueMap params;
    params["waveform_type"] = std::string{"constant"};
    params["num_channels"] = std::int64_t{2};
    params["batch_size"] = std::int64_t{16};
    params["amplitude"] = 4096.0;
    params["channel_phase_step"] = 3.14159265358979323846 / 2.0;

    cy::flowgraph::blocks::common::LFMSource source(params);
    auto writer = std::make_shared<CaptureWriter>();
    cy::flowgraph::blocks::io::DeviceSinkBlock<cy::common::CS16> sink(writer, 16);
    fg::connect(source.out, sink.in, 16);
    sink.on_start();
    source.process_work();
    sink.process_work();
    require(writer->bytes.size() == 16 * sizeof(cy::common::CS16),
            "Constant DeviceSink byte length mismatch");
    std::vector<cy::common::CS16> output(16);
    std::memcpy(output.data(), writer->bytes.data(), writer->bytes.size());
    for (std::size_t sample = 0; sample < 8; ++sample) {
        require(output[sample * 2] == cy::common::CS16{4096, 0},
                "Constant ch0 IQ layout mismatch");
        require(output[sample * 2 + 1] == cy::common::CS16{0, 4096},
                "Constant ch1 IQ layout mismatch");
    }
    require(sink.in.available() == 0,
            "Constant DeviceSink must consume the complete byte-exact batch");
}

void test_device_sink_write_length(std::size_t batch_elements, bool short_write) {
    fg::ValueMap params;
    params["waveform_type"] = std::string{"tone"};
    params["num_channels"] = std::int64_t{8};
    params["batch_size"] = static_cast<std::int64_t>(batch_elements);
    params["sample_rate"] = 30.72e6;
    params["tone_frequency"] = 960.0e3;
    params["amplitude"] = 4096.0;
    params["channel_phase_step"] = 0.0;

    cy::flowgraph::blocks::common::LFMSource source(params);
    auto writer = std::make_shared<CaptureWriter>(short_write ? sizeof(cy::common::CS16) : 0);
    cy::flowgraph::blocks::io::DeviceSinkBlock<cy::common::CS16> sink(
        writer, batch_elements);
    fg::connect(source.out, sink.in, batch_elements * 2);
    sink.on_start();
    source.process_work();
    sink.process_work();

    const std::size_t expected_bytes = batch_elements * sizeof(cy::common::CS16);
    require(writer->calls == 1 && writer->requested_bytes == expected_bytes,
            "DeviceSink requested byte length mismatch");
    require(sink.in.available() == (short_write ? batch_elements : 0),
            "DeviceSink short-write consumption behavior mismatch");
}

void test_lfm_to_device_sink_interleaved_passthrough() {
    fg::ValueMap params;
    params["num_channels"] = std::int64_t{2};
    params["samples_per_pulse"] = std::int64_t{512};
    params["batch_size"] = std::int64_t{256};

    cy::flowgraph::blocks::common::LFMSource source(params);
    auto writer = std::make_shared<CaptureWriter>();
    cy::flowgraph::blocks::io::DeviceSinkBlock<cy::common::CS16> sink(writer, 256);
    fg::connect(source.out, sink.in, 512);
    sink.on_start();

    source.process_work();
    sink.process_work();

    require(writer->bytes.size() == 256 * sizeof(cy::common::CS16),
            "LFM to DeviceSink must write one complete interleaved batch");
    std::vector<cy::common::CS16> output(256);
    std::memcpy(output.data(), writer->bytes.data(), writer->bytes.size());
    for (std::size_t i = 0; i < 200; ++i) {
        require(output[i] == cy::common::CS16{0, 0},
                "DeviceSink changed the interleaved delay interval");
    }
    require(output[200] != cy::common::CS16{0, 0} &&
                output[201] != cy::common::CS16{0, 0},
            "DeviceSink lost an interleaved channel sample");
    require(sink.in.available() == 0,
            "DeviceSink must consume the complete interleaved batch after writing");
}

void test_tx_lfm_config_fast_path_and_bit_exact() {
    fg::ValueMap params;
    params["num_channels"] = std::int64_t{8};
    params["samples_per_pulse"] = std::int64_t{512}; // template_size = 4096
    params["batch_size"] = std::int64_t{32768};     // 32768 % 4096 == 0

    cy::flowgraph::blocks::common::LFMSource source(params);
    fg::PortIn<cy::common::CS16> sink;
    fg::connect(source.out, sink, 65536);
    source.on_start();

    // 运行 2 次 process_work，共生成 65536 元素
    source.process_work();
    source.process_work();

    require(source.fast_path_hits() == 2, "32768 batch / 4096 template must hit fast path");
    require(source.fallback_memcpy_count() == 0, "Fast path must not trigger fallback memcpy");

    auto output = sink.get(65536);
    require(output.size() == 65536, "Must receive 65536 elements");

    // 验证完整 batch 是 4096-element 模板重复 8 次
    for (std::size_t batch = 0; batch < 2; ++batch) {
        const std::size_t batch_offset = batch * 32768;
        for (std::size_t rep = 1; rep < 8; ++rep) {
            for (std::size_t i = 0; i < 4096; ++i) {
                require(output[batch_offset + i] == output[batch_offset + rep * 4096 + i],
                        "Batch element mismatch across template repeats in fast path");
            }
        }
    }
    output.consume(output.size());
    std::cout << "  - TX LFM Config Fast Path & Bit-Exact Repetition test passed." << std::endl;
}

void test_non_divisible_batch_continuity() {
    fg::ValueMap params;
    params["num_channels"] = std::int64_t{8};
    params["samples_per_pulse"] = std::int64_t{512}; // template_size = 4096
    params["batch_size"] = std::int64_t{3000};      // 3000 不整除 4096，且为 8 的倍数

    cy::flowgraph::blocks::common::LFMSource source(params);
    fg::PortIn<cy::common::CS16> sink;
    fg::connect(source.out, sink, 30000);
    source.on_start();

    for (int i = 0; i < 10; ++i) {
        source.process_work();
    }

    require(source.fast_path_hits() == 0, "Non-divisible batch must not hit fast path");
    require(source.fallback_memcpy_count() > 0, "Non-divisible batch must use fallback memcpy");

    auto output = sink.get(30000);
    require(output.size() == 30000, "Must receive 30000 elements");

    // 取前 4096 作为基准模板参考
    std::vector<cy::common::CS16> ref_template(4096);
    for (std::size_t i = 0; i < 4096; ++i) {
        ref_template[i] = output[i];
    }

    // 验证跨模板边界和跨 batch 边界连续性
    for (std::size_t i = 0; i < 30000; ++i) {
        require(output[i] == ref_template[i % 4096],
                "Continuous wave continuity broken in non-divisible batch");
    }
    output.consume(output.size());
    std::cout << "  - Non-Divisible Batch Boundary Continuity test passed." << std::endl;
}

void test_small_reserve_and_ring_wrap() {
    fg::ValueMap params;
    params["num_channels"] = std::int64_t{8};
    params["samples_per_pulse"] = std::int64_t{512}; // template_size = 4096
    params["batch_size"] = std::int64_t{32768};

    cy::flowgraph::blocks::common::LFMSource source(params);
    fg::PortIn<cy::common::CS16> sink;
    // 故意提供较小/不匹配整倍数的 buffer 空间 10000 元素
    fg::connect(source.out, sink, 10000);
    source.on_start();

    source.process_work();

    require(source.fallback_memcpy_count() > 0, "Small reserve must fall back to chunked memcpy");

    auto output = sink.get(9992);
    require(output.size() == 9992, "Should get 9992 elements");

    std::vector<cy::common::CS16> ref_template(4096);
    for (std::size_t i = 0; i < 4096; ++i) {
        ref_template[i] = output[i];
    }
    for (std::size_t i = 0; i < 9992; ++i) {
        require(output[i] == ref_template[i % 4096],
                "Small reserve sample element mismatch");
    }
    output.consume(output.size());
    std::cout << "  - Small Reserve & Ring Wrap Fallback test passed." << std::endl;
}

void test_throughput_benchmark_5s() {
    std::cout << "\n=== Release Memory Consumer 5-Second Throughput Benchmark ===" << std::endl;

    fg::ValueMap params;
    params["num_channels"] = std::int64_t{8};
    params["samples_per_pulse"] = std::int64_t{512}; // template_size = 4096
    params["batch_size"] = std::int64_t{32768};     // 32768 % 4096 == 0

    cy::flowgraph::blocks::common::LFMSource source(params);
    fg::PortIn<cy::common::CS16> sink;
    constexpr std::size_t kRingCapacity = 262144; // 256K CS16 元素
    fg::connect(source.out, sink, kRingCapacity);
    source.on_start();

    const auto start_time = std::chrono::steady_clock::now();
    const auto target_duration = std::chrono::seconds(5);

    std::uint64_t total_committed_elements = 0;

    while (std::chrono::steady_clock::now() - start_time < target_duration) {
        source.process_work();
        auto span = sink.get(kRingCapacity);
        if (!span.empty()) {
            total_committed_elements += span.size();
            span.consume(span.size());
        }
    }
    const auto end_time = std::chrono::steady_clock::now();
    const double elapsed_sec =
        std::chrono::duration<double>(end_time - start_time).count();

    const double elements_per_sec =
        static_cast<double>(total_committed_elements) / elapsed_sec;

    std::cout << "  [Benchmark Results]" << std::endl;
    std::cout << "    Elapsed Time        : " << elapsed_sec << " s" << std::endl;
    std::cout << "    process_work Calls  : " << source.process_work_calls() << std::endl;
    std::cout << "    Commit Elements     : " << total_committed_elements << " CS16" << std::endl;
    std::cout << "    Throughput Rate     : " << elements_per_sec / 1e6 << " M elements/s" << std::endl;
    std::cout << "    Fast Path Hits      : " << source.fast_path_hits() << std::endl;
    std::cout << "    Fallback Memcpy Hits: " << source.fallback_memcpy_count() << std::endl;
    std::cout << "=========================================================\n" << std::endl;

    require(source.fast_path_hits() > 0, "Benchmark must hit fast path");
}

} // namespace

int main() {
    std::cout << "Starting qa_lfm_source_block 8-channel test..." << std::endl;

    test_configurable_two_channel_layout();
    test_invalid_batch_shape();
    test_invalid_diagnostic_params();
    test_coherent_tone_batch_boundary(2048);
    test_coherent_tone_batch_boundary(4096);
    test_constant_channel_iq_layout();
    test_device_sink_write_length(16384, false);
    test_device_sink_write_length(32768, false);
    test_device_sink_write_length(32768, true);
    test_lfm_to_device_sink_interleaved_passthrough();
    test_tx_lfm_config_fast_path_and_bit_exact();
    test_non_divisible_batch_continuity();
    test_small_reserve_and_ring_wrap();

    // 💡 1. 建立打流连接拓扑：LFMSource (out) -> sink (in)
    cy::flowgraph::ValueMap params;
    params["num_channels"] = std::int64_t{8};
    params["samples_per_pulse"] = std::int64_t{512};
    params["batch_size"] = std::int64_t{128};
    cy::flowgraph::blocks::common::LFMSource source(params);
    require(source.num_channels() == 8, "Configured channel count mismatch");
    require(source.total_elements() == 8 * 512, "Configured pulse element count mismatch");
    fg::PortIn<cy::common::CS16> sink;
    fg::connect(source.out, sink, 200000);

    // 💡 2. 循环调用 process_work 64次产生足够样点
    // 8 通道 * 2 脉冲 * 512 样点 = 8192 点
    // 每次生成 128 点，因此执行 64 次可以恰好生成 64 * 128 = 8192 点
    for (int i = 0; i < 64; ++i) {
        source.process_work();
    }

    auto output = sink.get(8192);
    require(output.size() == 8192, "Output sample size should be exactly 8192");

    // 💡 3. 四大核心断言自检
    
    // A. 时延零值区间断言 (Range Delay Nulling)
    // kRangeBin = 100.0，因此对于每个通道，前 100 个时间采样点都必须是零
    // 交织格式下，前 8 * 100 = 800 个点都应当是零
    for (std::size_t i = 0; i < 800; ++i) {
        require(output[i].i == 0 && output[i].q == 0, "Delay interval samples must be zero on all channels");
    }
    std::cout << "  - 8-Channel Range Delay Nulling assertion passed." << std::endl;

    // B. 脉宽边界与截断断言 (Pulse Width Cutoff)
    // 第 0 个脉冲（前 4096 点），第 0 通道的第 100 点对应 idx = 100 * 8 = 800，应当不为 0
    require(output[800].i != 0 || output[800].q != 0, "Start of echo sample (ch0) must not be zero");
    // 第 0 通道脉宽截断点在 356 点，对应 idx = 356 * 8 = 2848，此点及之后到周期结束都应为 0
    for (std::size_t sample = 356; sample < 512; ++sample) {
        for (std::size_t ch = 0; ch < 8; ++ch) {
            std::size_t idx = sample * 8 + ch;
            require(output[idx].i == 0 && output[idx].q == 0, "Samples after pulse width must be zero");
        }
    }
    std::cout << "  - 8-Channel Pulse Width Cutoff assertion passed." << std::endl;

    // C. 信号幅度量化断言 (Amplitude Quantization)
    // 脉宽内模值应当等于 amplitude = 8191.0，允许 +/- 2.5 量化舍入抖动
    for (std::size_t sample = 100; sample < 356; ++sample) {
        for (std::size_t ch = 0; ch < 8; ++ch) {
            std::size_t idx = sample * 8 + ch;
            double magnitude = std::sqrt(static_cast<double>(output[idx].i) * output[idx].i + static_cast<double>(output[idx].q) * output[idx].q);
            require(std::abs(magnitude - 8191.0) < 2.5, "Magnitude of echo must align with Amplitude");
        }
    }
    std::cout << "  - 8-Channel Amplitude Quantization assertion passed." << std::endl;

    // D. 单脉冲模板循环一致性与阵元通道相位步进断言
    // ch=0, pulse=0, sample=100 -> idx = 100 * 8 = 800
    double theta_p0_ch0 = std::atan2(static_cast<double>(output[800].q), static_cast<double>(output[800].i));
    
    // ch=1, pulse=0, sample=100 -> idx = 100 * 8 + 1 = 801
    double theta_p0_ch1 = std::atan2(static_cast<double>(output[801].q), static_cast<double>(output[801].i));

    // 1) 第二个脉冲必须逐元素复现第一个脉冲。
    constexpr std::size_t pulse_elements = 8 * 512;
    for (std::size_t i = 0; i < pulse_elements; ++i) {
        require(output[i] == output[pulse_elements + i],
                "Repeated pulse must match the single-pulse template exactly");
    }
    std::cout << "  - Continuous Single-Pulse Repetition assertion passed." << std::endl;

    // 2) 校验空间通道相移: channel_phase_step = 0.5 rad
    double spatial_phase_diff = theta_p0_ch1 - theta_p0_ch0;
    while (spatial_phase_diff < -3.141592653589793) spatial_phase_diff += 2.0 * 3.141592653589793;
    while (spatial_phase_diff > 3.141592653589793) spatial_phase_diff -= 2.0 * 3.141592653589793;
    
    require(std::abs(spatial_phase_diff - 0.5) < 1e-4, "Coherent Spatial phase step mismatch");
    std::cout << "  - Coherent Spatial Phase Step assertion passed. Expected: 0.5 rad, Got: " 
              << spatial_phase_diff << " rad" << std::endl;

    output.consume(8192);

    test_throughput_benchmark_5s();

    std::cout << "8-Channel LFM source block test passed." << std::endl;
    return 0;
}
