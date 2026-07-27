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
    data::InputData input{};
    input.sample_count = 32;
    for (std::size_t i = 0; i < input.sample_count; ++i) {
        input.samples[i] = offset + static_cast<float>(i) * 0.25F;
    }
    return input;
}

fg::ValueMap MakeParams(double factor) {
    return {{"factor", factor}};
}

std::vector<std::byte> EncodeInput(
    const data::InputData& input,
    sdk::FrameMetadata metadata = {17, 1234}) {
    std::vector<std::byte> payload(sizeof(input));
    Require(sdk::TrivialFrameCodec<data::InputData>::encode(
                input,
                cy::common::Span<std::byte>(
                    payload.data(), payload.size())),
            "POD 输入复制失败");
    std::vector<std::byte> wire(sdk::WireFrameBytes(payload.size()));
    Require(sdk::EncodeFrame(
                cy::common::Span<const std::byte>(
                    payload.data(), payload.size()),
                metadata,
                cy::common::Span<std::byte>(
                    wire.data(), wire.size())),
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
                    0,
                    cy::common::Span<std::byte>(
                        bytes.data(), bytes.size())) == bytes.size(),
                "测试输出读取失败");
        Require(sink.consume_exact(bytes.size()), "测试输出消费失败");
    }
    return bytes;
}

void VerifyOutput(
    const std::vector<std::byte>& wire,
    const data::InputData& input,
    float factor,
    sdk::FrameMetadata expected_metadata = {17, 1234}) {
    const auto inspection = sdk::InspectFrame(
        cy::common::Span<const std::byte>(
            wire.data(), wire.size()),
        wire.size());
    Require(inspection.status == sdk::FrameParseStatus::CompleteFrame,
            "输出不是完整合法帧");
    Require(inspection.payload_bytes == sizeof(data::OutputData),
            "输出 POD Payload 大小错误");
    Require(inspection.metadata.sequence_id ==
                expected_metadata.sequence_id &&
                inspection.metadata.timestamp_unix_nano ==
                    expected_metadata.timestamp_unix_nano,
            "输出元数据没有正确透传");

    data::OutputData output{};
    Require(sdk::TrivialFrameCodec<data::OutputData>::decode(
                cy::common::Span<const std::byte>(
                    wire.data() + sdk::kFrameEnvelopeBytes,
                    inspection.payload_bytes),
                output),
            "输出 POD 复制失败");
    Require(output.sample_count == input.sample_count, "输出样点数错误");
    for (std::size_t i = 0; i < output.sample_count; ++i) {
        const float expected = input.samples[i] * factor;
        Require(std::abs(output.samples[i] - expected) < 1.0e-6F,
                "输出数值错误，索引=" + std::to_string(i));
    }
}

void TestPODTransportAndAlgorithm() {
    const auto input = MakeInput();
    Require(
        sdk::TrivialFrameCodec<data::InputData>::encoded_size(input) ==
            sizeof(input),
        "POD Payload 大小不是 sizeof(InputData)");

    std::vector<std::byte> payload(sizeof(input));
    Require(sdk::TrivialFrameCodec<data::InputData>::encode(
                input,
                cy::common::Span<std::byte>(
                    payload.data(), payload.size())),
            "POD 编码失败");
    Require(std::memcmp(payload.data(), &input, sizeof(input)) == 0,
            "POD 编码没有保持完整对象表示");

    data::InputData decoded{};
    Require(sdk::TrivialFrameCodec<data::InputData>::decode(
                cy::common::Span<const std::byte>(
                    payload.data(), payload.size()),
                decoded),
            "POD 解码失败");
    Require(std::memcmp(&decoded, &input, sizeof(input)) == 0,
            "POD round-trip 不是逐字节无损");
    Require(!sdk::TrivialFrameCodec<data::InputData>::decode(
                cy::common::Span<const std::byte>(
                    payload.data(), payload.size() - 1),
                decoded),
            "POD 解码接受了截断 Payload");

    payload.push_back(std::byte{0});
    Require(!sdk::TrivialFrameCodec<data::InputData>::decode(
                cy::common::Span<const std::byte>(
                    payload.data(), payload.size()),
                decoded),
            "POD 解码接受了额外尾字节");

    const auto params = MakeParams(2.0);
    MyAlgorithm algorithm{sdk::Params(params)};
    data::OutputData output{};
    Require(algorithm.work(input, output) ==
                sdk::ProcessResult::Produced,
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
        sdk::FrameAlgorithmAdapter<MyAlgorithm> adapter(
            MakeParams(3.0));
        fg::PortIn<std::byte> sink;
        fg::connect(source, adapter.in, capacity);
        fg::connect(adapter.out, sink, capacity);

        Publish(source, wire.data(), split);
        const std::size_t available = adapter.in.available();
        Require(!adapter.process_work(), "残帧错误触发了算法");
        Require(adapter.in.available() == available,
                "残帧推进了输入游标");
        Require(adapter.stats().frames_processed == 0,
                "残帧被计为已处理帧");

        Publish(source, wire.data() + split, wire.size() - split);
        Require(adapter.process_work(), "完整帧没有推动 Adapter");
        Require(adapter.stats().frames_processed == 1,
                "完整帧处理次数错误");
        VerifyOutput(Drain(sink), input, 3.0F);
    }
}

void TestWrongLengthResynchronises() {
    const auto input = MakeInput();
    const auto valid_wire = EncodeInput(input, {123, 456});
    for (const std::int64_t delta : {-1, 1}) {
        auto invalid_wire = valid_wire;
        sdk::detail::write_u64_le(
            invalid_wire.data() + 8,
            static_cast<std::uint64_t>(
                static_cast<std::int64_t>(invalid_wire.size()) +
                delta));

        const std::size_t capacity = valid_wire.size() * 3;
        fg::PortOut<std::byte> source;
        sdk::FrameAlgorithmAdapter<MyAlgorithm> adapter(
            MakeParams(1.0));
        fg::PortIn<std::byte> sink;
        fg::connect(source, adapter.in, capacity);
        fg::connect(adapter.out, sink, capacity);

        Publish(source, invalid_wire.data(), invalid_wire.size());
        Publish(source, valid_wire.data(), valid_wire.size());
        for (std::size_t attempt = 0;
             attempt < invalid_wire.size() + 64 &&
             adapter.stats().frames_emitted == 0;
             ++attempt) {
            (void)adapter.process_work();
        }

        Require(adapter.stats().invalid_length != 0,
                "错长 POD 帧没有被拒绝");
        Require(adapter.stats().frames_processed == 1,
                "错长帧触发了算法或阻塞了后续合法帧");
        VerifyOutput(Drain(sink), input, 1.0F, {123, 456});
    }
}

void TestBackpressureRunsAlgorithmOnce() {
    const auto input = MakeInput();
    const auto wire = EncodeInput(input, {99, 5678});
    const std::size_t output_capacity = wire.size() / 2;
    fg::PortOut<std::byte> source;
    sdk::FrameAlgorithmAdapter<MyAlgorithm> adapter(MakeParams(0.5));
    fg::PortIn<std::byte> sink;
    fg::connect(source, adapter.in, wire.size() * 2);
    fg::connect(adapter.out, sink, output_capacity);

    Publish(source, wire.data(), wire.size());
    Require(adapter.process_work(), "背压场景首次工作没有进展");
    Require(adapter.has_pending_output(), "受阻输出未被保留");
    Require(adapter.stats().frames_processed == 1, "算法执行次数错误");
    Require(!adapter.process_work(), "输出满时不应报告进展");
    Require(adapter.stats().frames_processed == 1,
            "背压导致算法重复执行");

    std::vector<std::byte> assembled;
    while (adapter.has_pending_output() || sink.available() != 0) {
        auto chunk = Drain(sink);
        assembled.insert(
            assembled.end(), chunk.begin(), chunk.end());
        if (adapter.has_pending_output()) {
            Require(adapter.process_work(),
                    "释放背压后输出没有继续排空");
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
    sdk::FrameAlgorithmAdapter<MyAlgorithm> adapter(MakeParams(1.0));
    fg::PortIn<std::byte> sink;
    fg::connect(source, adapter.in, capacity);
    fg::connect(adapter.out, sink, capacity);

    for (std::size_t frame = 0; frame < 8; ++frame) {
        Publish(source, wire.data(), wire.size());
        Require(adapter.process_work(), "环形折返场景处理失败");
        std::vector<std::byte> assembled;
        while (adapter.has_pending_output() || sink.available() != 0) {
            auto chunk = Drain(sink);
            assembled.insert(
                assembled.end(), chunk.begin(), chunk.end());
            if (adapter.has_pending_output()) {
                Require(adapter.process_work(),
                        "折返输出没有继续排空");
            }
        }
        VerifyOutput(assembled, input, 1.0F);
    }
}

} // namespace

int main() {
    TestPODTransportAndAlgorithm();
    TestEveryByteSplit();
    TestWrongLengthResynchronises();
    TestBackpressureRunsAlgorithmOnce();
    TestRingWrap();
    std::cout << "qa_algorithm_block passed\n";
    return 0;
}
