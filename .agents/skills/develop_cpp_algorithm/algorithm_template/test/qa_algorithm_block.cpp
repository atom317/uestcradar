#include "algorithm.h"

#include <cycore_algorithm_sdk.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace data = cycore::algorithm::my_block;
namespace fg = cy::flowgraph;
namespace sdk = cycore::sdk;

namespace {

void Require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

data::InputData MakeInput(float offset = 0.0F) {
    data::InputData input;
    input.sample_count = 32;
    for (std::size_t i = 0; i < input.sample_count; ++i) {
        input.samples[i] = offset + static_cast<float>(i) * 0.25F;
    }
    return input;
}

std::vector<std::byte> EncodeInput(const data::InputData& input,
                                   sdk::FrameMetadata metadata = {17, 1234}) {
    const std::size_t payload_bytes =
        sdk::FrameCodec<data::InputData>::encoded_size(input);
    std::vector<std::byte> payload(payload_bytes);
    Require(sdk::FrameCodec<data::InputData>::encode(
                input, cy::common::Span<std::byte>(payload.data(), payload.size())),
            "输入 Codec 编码失败");
    std::vector<std::byte> wire(sdk::WireFrameBytes(payload.size()));
    Require(sdk::EncodeFrame(
                cy::common::Span<const std::byte>(payload.data(), payload.size()),
                metadata,
                cy::common::Span<std::byte>(wire.data(), wire.size())),
            "SDK 输入封帧失败");
    return wire;
}

void Publish(fg::PortOut<std::byte>& source,
             const std::byte* bytes,
             std::size_t size) {
    std::size_t offset = 0;
    while (offset < size) {
        auto span = source.reserve(size - offset);
        Require(!span.empty(), "测试输入端发生意外背压");
        std::memcpy(span.data(), bytes + offset, span.size());
        offset += span.size();
        span.commit(span.size());
    }
}

std::vector<std::byte> Drain(fg::PortIn<std::byte>& sink) {
    std::vector<std::byte> bytes(sink.available());
    if (!bytes.empty()) {
        Require(sink.peek_copy(
                    0, cy::common::Span<std::byte>(bytes.data(), bytes.size())) ==
                    bytes.size(),
                "测试输出读取失败");
        Require(sink.consume_exact(bytes.size()), "测试输出消费失败");
    }
    return bytes;
}

void VerifyOutput(const std::vector<std::byte>& wire,
                  const data::InputData& input,
                  float factor,
                  sdk::FrameMetadata expected_metadata = {17, 1234}) {
    const auto inspection = sdk::InspectFrame(
        cy::common::Span<const std::byte>(wire.data(), wire.size()), wire.size());
    Require(inspection.status == sdk::FrameParseStatus::CompleteFrame,
            "输出不是完整合法帧");
    Require(inspection.metadata.sequence_id == expected_metadata.sequence_id &&
                inspection.metadata.timestamp_unix_nano ==
                    expected_metadata.timestamp_unix_nano,
            "输出元数据没有正确透传");
    data::OutputData output;
    Require(sdk::FrameCodec<data::OutputData>::decode(
                cy::common::Span<const std::byte>(
                    wire.data() + sdk::kFrameEnvelopeBytes,
                    inspection.payload_bytes),
                output),
            "输出 Codec 解码失败");
    Require(output.sample_count == input.sample_count, "输出样点数错误");
    for (std::size_t i = 0; i < output.sample_count; ++i) {
        const float expected = input.samples[i] * factor;
        Require(std::abs(output.samples[i] - expected) < 1.0e-6F,
                "输出数值错误，索引=" + std::to_string(i));
    }
}

fg::ValueMap MakeParams(double factor, std::size_t maximum_wire_bytes) {
    return {
        {"factor", factor},
        {"max_input_frame_bytes", static_cast<std::int64_t>(maximum_wire_bytes)},
        {"max_output_frame_bytes", static_cast<std::int64_t>(maximum_wire_bytes)},
    };
}

void TestCodecAndAlgorithm() {
    const auto input = MakeInput();
    const std::size_t payload_bytes =
        sdk::FrameCodec<data::InputData>::encoded_size(input);
    std::vector<std::byte> payload(payload_bytes);
    Require(sdk::FrameCodec<data::InputData>::encode(
                input, cy::common::Span<std::byte>(payload.data(), payload.size())),
            "Codec 编码失败");
    data::InputData decoded;
    Require(sdk::FrameCodec<data::InputData>::decode(
                cy::common::Span<const std::byte>(payload.data(), payload.size()),
                decoded),
            "Codec 解码失败");
    Require(decoded.sample_count == input.sample_count, "Codec 样点数错误");
    Require(!sdk::FrameCodec<data::InputData>::decode(
                cy::common::Span<const std::byte>(
                    payload.data(), payload.size() - 1),
                decoded),
            "Codec 接受了截断 Payload");

    MyAlgorithm algorithm(sdk::Params(MakeParams(2.0, 8192)));
    data::OutputData output;
    Require(algorithm.work(input, output) == sdk::ProcessResult::Produced,
            "强类型算法没有产生输出");
    Require(output.samples[7] == input.samples[7] * 2.0F,
            "强类型算法金标值错误");
}

void TestEveryByteSplit() {
    const auto input = MakeInput();
    const auto wire = EncodeInput(input);
    const std::size_t capacity = wire.size() * 2 + 17;
    for (std::size_t split = 1; split < wire.size(); ++split) {
        fg::PortOut<std::byte> source;
        sdk::FrameAlgorithmAdapter<MyAlgorithm> adapter(MakeParams(3.0, capacity));
        fg::PortIn<std::byte> sink;
        fg::connect(source, adapter.in, capacity);
        fg::connect(adapter.out, sink, capacity);

        Publish(source, wire.data(), split);
        const std::size_t available = adapter.in.available();
        Require(!adapter.process_work(), "残帧错误触发了算法");
        Require(adapter.in.available() == available, "残帧推进了输入游标");
        Require(adapter.stats().frames_processed == 0, "残帧被计为已处理帧");

        Publish(source, wire.data() + split, wire.size() - split);
        Require(adapter.process_work(), "完整帧没有推动 Adapter");
        Require(adapter.stats().frames_processed == 1, "完整帧处理次数错误");
        VerifyOutput(Drain(sink), input, 3.0F);
    }
}

void TestBackpressureRunsAlgorithmOnce() {
    const auto input = MakeInput();
    const auto wire = EncodeInput(input, {99, 5678});
    const std::size_t output_capacity = wire.size() / 2;
    fg::PortOut<std::byte> source;
    sdk::FrameAlgorithmAdapter<MyAlgorithm> adapter(MakeParams(0.5, wire.size() * 2));
    fg::PortIn<std::byte> sink;
    fg::connect(source, adapter.in, wire.size() * 2);
    fg::connect(adapter.out, sink, output_capacity);

    Publish(source, wire.data(), wire.size());
    Require(adapter.process_work(), "背压场景首次工作没有进展");
    Require(adapter.has_pending_output(), "受阻输出未被保留");
    Require(adapter.stats().frames_processed == 1, "算法执行次数错误");
    Require(!adapter.process_work(), "输出满时不应报告进展");
    Require(adapter.stats().frames_processed == 1, "背压导致算法重复执行");

    std::vector<std::byte> assembled;
    while (adapter.has_pending_output() || sink.available() != 0) {
        auto chunk = Drain(sink);
        assembled.insert(assembled.end(), chunk.begin(), chunk.end());
        if (adapter.has_pending_output()) {
            Require(adapter.process_work(), "释放背压后输出没有继续排空");
            Require(adapter.stats().frames_processed == 1,
                    "排空输出时算法被重复执行");
        }
    }
    VerifyOutput(assembled, input, 0.5F, {99, 5678});
}

void TestRingWrap() {
    const auto input = MakeInput();
    const auto wire = EncodeInput(input);
    const std::size_t capacity = wire.size() + 17;
    fg::PortOut<std::byte> source;
    sdk::FrameAlgorithmAdapter<MyAlgorithm> adapter(MakeParams(1.0, capacity));
    fg::PortIn<std::byte> sink;
    fg::connect(source, adapter.in, capacity);
    fg::connect(adapter.out, sink, capacity);

    for (std::size_t frame = 0; frame < 8; ++frame) {
        Publish(source, wire.data(), wire.size());
        Require(adapter.process_work(), "环形折返场景处理失败");
        std::vector<std::byte> assembled;
        while (adapter.has_pending_output() || sink.available() != 0) {
            auto chunk = Drain(sink);
            assembled.insert(assembled.end(), chunk.begin(), chunk.end());
            if (adapter.has_pending_output()) {
                Require(adapter.process_work(), "折返输出没有继续排空");
            }
        }
        VerifyOutput(assembled, input, 1.0F);
    }
}

} // namespace

int main() {
    TestCodecAndAlgorithm();
    TestEveryByteSplit();
    TestBackpressureRunsAlgorithmOnce();
    TestRingWrap();
    std::cout << "qa_algorithm_block passed\n";
    return 0;
}
