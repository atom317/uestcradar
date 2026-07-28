#include "algorithm.h"

#include <cycore_algorithm_sdk.h>

#include <array>
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

constexpr std::size_t kMaximumInputWireBytes =
    sdk::kFrameEnvelopeBytes + sizeof(data::InputHeader) +
    data::kMaxSamples * sizeof(float);
constexpr std::size_t kMaximumOutputWireBytes =
    sdk::kFrameEnvelopeBytes + sizeof(data::OutputHeader) +
    data::kMaxSamples * sizeof(float);

void Require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

data::InputData MakeInput(std::size_t count = 32, float offset = 0.0F) {
    data::InputData input;
    input.header.sample_count = static_cast<std::uint32_t>(count);
    input.payload.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        input.payload[i] = offset + static_cast<float>(i) * 0.25F;
    }
    return input;
}

fg::ValueMap MakeParams(double factor) {
    return {
        {"factor", factor},
        {"max_input_frame_bytes",
         static_cast<std::int64_t>(kMaximumInputWireBytes)},
        {"max_output_frame_bytes",
         static_cast<std::int64_t>(kMaximumOutputWireBytes)},
    };
}

template <typename Frame>
std::vector<std::byte> EncodePayload(const Frame& frame) {
    using Codec = sdk::FrameDataCodec<Frame>;
    std::vector<std::byte> payload(Codec::encoded_size(frame));
    Require(Codec::encode(
                frame,
                cy::common::Span<std::byte>(
                    payload.data(), payload.size())),
            "业务帧编码失败");
    return payload;
}

std::vector<std::byte> EncodeInput(
    const data::InputData& input,
    sdk::FrameMetadata metadata = {17, 1234}) {
    const auto payload = EncodePayload(input);
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

void Publish(fg::PortOut<std::byte>& source,
             const std::vector<std::byte>& bytes) {
    Publish(source, bytes.data(), bytes.size());
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
        kMaximumOutputWireBytes);
    Require(inspection.status == sdk::FrameParseStatus::CompleteFrame,
            "输出不是完整合法帧");
    Require(inspection.payload_bytes ==
                sizeof(data::OutputHeader) +
                    input.payload.size() * sizeof(float),
            "输出变长 Payload 大小错误");
    Require(inspection.metadata.sequence_id ==
                expected_metadata.sequence_id &&
                inspection.metadata.timestamp_unix_nano ==
                    expected_metadata.timestamp_unix_nano,
            "输出元数据没有正确透传");

    data::OutputData output;
    output.payload.reserve(data::kMaxSamples);
    Require(sdk::FrameDataCodec<data::OutputData>::decode(
                cy::common::Span<const std::byte>(
                    wire.data() + sdk::kFrameEnvelopeBytes,
                    inspection.payload_bytes),
                output),
            "输出业务帧解码失败");
    Require(output.header.sample_count == input.header.sample_count,
            "输出样点数错误");
    Require(output.payload.size() == input.payload.size(),
            "输出 vector 长度错误");
    for (std::size_t i = 0; i < output.payload.size(); ++i) {
        const float expected = input.payload[i] * factor;
        Require(std::abs(output.payload[i] - expected) < 1.0e-6F,
                "输出数值错误，索引=" + std::to_string(i));
    }
}

void TestFixedPodCompatibility() {
    struct FixedPod {
        std::uint32_t count;
        std::array<float, 4> values;
    };
    static_assert(sdk::is_supported_frame_v<FixedPod>);

    const FixedPod input{4, {1.0F, 2.0F, 3.0F, 4.0F}};
    std::array<std::byte, sizeof(FixedPod)> bytes{};
    Require(sdk::FrameDataCodec<FixedPod>::encode(
                input,
                cy::common::Span<std::byte>(
                    bytes.data(), bytes.size())),
            "固定 POD 兼容编码失败");
    FixedPod output{};
    Require(sdk::FrameDataCodec<FixedPod>::decode(
                cy::common::Span<const std::byte>(
                    bytes.data(), bytes.size()),
                output),
            "固定 POD 兼容解码失败");
    Require(std::memcmp(&input, &output, sizeof(input)) == 0,
            "固定 POD 兼容 round-trip 失败");
}

void TestVectorCodecAndAlgorithm() {
    static_assert(sdk::is_vector_frame_v<data::InputData>);
    static_assert(sdk::is_vector_frame_v<data::OutputData>);

    const auto input = MakeInput();
    const auto payload = EncodePayload(input);
    Require(payload.size() ==
                sizeof(data::InputHeader) +
                    input.payload.size() * sizeof(float),
            "vector Payload 包含了多余容量");

    data::InputData decoded;
    decoded.payload.reserve(data::kMaxSamples);
    Require(sdk::FrameDataCodec<data::InputData>::decode(
                cy::common::Span<const std::byte>(
                    payload.data(), payload.size()),
                decoded),
            "vector Payload 解码失败");
    Require(decoded.header.sample_count == input.header.sample_count &&
                decoded.payload == input.payload,
            "vector Payload round-trip 失败");

    auto malformed = payload;
    malformed.push_back(std::byte{0});
    Require(!sdk::FrameDataCodec<data::InputData>::decode(
                cy::common::Span<const std::byte>(
                    malformed.data(), malformed.size()),
                decoded),
            "不能整除元素大小的 Payload 被接受");

    data::InputData empty;
    const auto empty_payload = EncodePayload(empty);
    Require(empty_payload.size() == sizeof(data::InputHeader),
            "空 vector 帧大小错误");
    decoded.payload.clear();
    Require(sdk::FrameDataCodec<data::InputData>::decode(
                cy::common::Span<const std::byte>(
                    empty_payload.data(), empty_payload.size()),
                decoded) &&
                decoded.payload.empty(),
            "空 vector 帧解码失败");

    const auto params = MakeParams(2.0);
    MyAlgorithm algorithm{sdk::Params(params)};
    data::OutputData output;
    output.payload.reserve(data::kMaxSamples);
    Require(algorithm.work(input, output) ==
                sdk::ProcessResult::Produced,
            "强类型算法没有产生输出");
    Require(output.payload[7] == input.payload[7] * 2.0F,
            "强类型算法金标值错误");

    auto invalid = input;
    ++invalid.header.sample_count;
    Require(algorithm.work(invalid, output) == sdk::ProcessResult::Drop,
            "业务长度不一致的帧没有 Drop");
}

void TestLimitsAreRequired() {
    bool threw = false;
    try {
        sdk::FrameAlgorithmAdapter<MyAlgorithm> adapter({{"factor", 1.0}});
        (void)adapter;
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    Require(threw, "变长帧缺少最大字节配置时没有 Fail-Fast");
}

void TestEveryByteSplit() {
    const auto input = MakeInput(17);
    const auto wire = EncodeInput(input);
    for (std::size_t split = 1; split < wire.size(); ++split) {
        fg::PortOut<std::byte> source;
        sdk::FrameAlgorithmAdapter<MyAlgorithm> adapter(MakeParams(3.0));
        fg::PortIn<std::byte> sink;
        fg::connect(source, adapter.in, kMaximumInputWireBytes);
        fg::connect(adapter.out, sink, kMaximumOutputWireBytes);

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

void TestMalformedPayloadAndOversize() {
    const auto input = MakeInput(9);
    const auto valid_wire = EncodeInput(input, {123, 456});
    auto malformed_payload = EncodePayload(input);
    malformed_payload.push_back(std::byte{0});
    std::vector<std::byte> malformed_wire(
        sdk::WireFrameBytes(malformed_payload.size()));
    Require(sdk::EncodeFrame(
                cy::common::Span<const std::byte>(
                    malformed_payload.data(), malformed_payload.size()),
                {122, 455},
                cy::common::Span<std::byte>(
                    malformed_wire.data(), malformed_wire.size())),
            "畸形测试帧封包失败");

    fg::PortOut<std::byte> source;
    sdk::FrameAlgorithmAdapter<MyAlgorithm> adapter(MakeParams(1.0));
    fg::PortIn<std::byte> sink;
    fg::connect(source, adapter.in, kMaximumInputWireBytes * 2);
    fg::connect(adapter.out, sink, kMaximumOutputWireBytes);

    Publish(source, malformed_wire);
    Require(adapter.process_work(), "畸形完整帧没有被消费");
    Require(adapter.stats().codec_failures == 1 &&
                adapter.stats().frames_processed == 0,
            "畸形 Payload 触发了算法");

    Publish(source, valid_wire);
    Require(adapter.process_work(), "畸形帧阻塞了后续合法帧");
    VerifyOutput(Drain(sink), input, 1.0F, {123, 456});

    auto oversized_header = valid_wire;
    sdk::detail::write_u64_le(
        oversized_header.data() + 8,
        static_cast<std::uint64_t>(kMaximumInputWireBytes + 1));
    fg::PortOut<std::byte> oversize_source;
    sdk::FrameAlgorithmAdapter<MyAlgorithm> oversize_adapter(MakeParams(1.0));
    fg::PortIn<std::byte> oversize_sink;
    fg::connect(
        oversize_source, oversize_adapter.in, kMaximumInputWireBytes);
    fg::connect(
        oversize_adapter.out, oversize_sink, kMaximumOutputWireBytes);
    Publish(
        oversize_source,
        oversized_header.data(),
        sdk::kFrameEnvelopeBytes);
    Require(oversize_adapter.process_work(), "超限帧头没有被拒绝");
    Require(oversize_adapter.stats().invalid_length == 1 &&
                oversize_adapter.stats().frames_processed == 0,
            "超限帧触发了算法");
}

void TestVariableLengthsAndWrap() {
    fg::PortOut<std::byte> source;
    sdk::FrameAlgorithmAdapter<MyAlgorithm> adapter(MakeParams(1.5));
    fg::PortIn<std::byte> sink;
    fg::connect(source, adapter.in, kMaximumInputWireBytes);
    fg::connect(adapter.out, sink, kMaximumOutputWireBytes);

    const std::array<std::size_t, 7> counts{1, 33, 7, 128, 3, 64, 2};
    for (std::size_t frame = 0; frame < counts.size(); ++frame) {
        const auto input = MakeInput(counts[frame], static_cast<float>(frame));
        const sdk::FrameMetadata metadata{frame + 1, frame + 100};
        Publish(source, EncodeInput(input, metadata));
        Require(adapter.process_work(), "变长/折返帧处理失败");

        std::vector<std::byte> assembled;
        while (adapter.has_pending_output() || sink.available() != 0) {
            auto chunk = Drain(sink);
            assembled.insert(
                assembled.end(), chunk.begin(), chunk.end());
            if (adapter.has_pending_output()) {
                Require(adapter.process_work(), "折返输出没有继续排空");
            }
        }
        VerifyOutput(assembled, input, 1.5F, metadata);
    }
}

void TestDropAndBackpressure() {
    auto invalid = MakeInput(4);
    invalid.header.sample_count = 5;

    fg::PortOut<std::byte> drop_source;
    sdk::FrameAlgorithmAdapter<MyAlgorithm> drop_adapter(MakeParams(1.0));
    fg::PortIn<std::byte> drop_sink;
    fg::connect(drop_source, drop_adapter.in, kMaximumInputWireBytes);
    fg::connect(drop_adapter.out, drop_sink, kMaximumOutputWireBytes);
    Publish(drop_source, EncodeInput(invalid));
    Require(drop_adapter.process_work(), "Drop 帧没有被消费");
    Require(drop_adapter.stats().frames_dropped == 1 &&
                drop_adapter.stats().frames_emitted == 0 &&
                drop_sink.available() == 0,
            "Drop 帧产生了输出");

    const auto input = MakeInput(64);
    const auto wire = EncodeInput(input, {99, 5678});
    fg::PortOut<std::byte> source;
    sdk::FrameAlgorithmAdapter<MyAlgorithm> adapter(MakeParams(0.5));
    fg::PortIn<std::byte> sink;
    fg::connect(source, adapter.in, kMaximumInputWireBytes);
    fg::connect(adapter.out, sink, wire.size() / 2);

    Publish(source, wire);
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
            Require(adapter.process_work(), "释放背压后输出没有继续排空");
            Require(adapter.stats().frames_processed == 1,
                    "排空输出时算法被重复执行");
        }
    }
    VerifyOutput(assembled, input, 0.5F, {99, 5678});
}

void TestOutputLimit() {
    auto params = MakeParams(1.0);
    params["max_output_frame_bytes"] =
        static_cast<std::int64_t>(
            sdk::kFrameEnvelopeBytes + sizeof(data::OutputHeader) +
            4 * sizeof(float));

    fg::PortOut<std::byte> source;
    sdk::FrameAlgorithmAdapter<MyAlgorithm> adapter(params);
    fg::PortIn<std::byte> sink;
    fg::connect(source, adapter.in, kMaximumInputWireBytes);
    fg::connect(adapter.out, sink, kMaximumOutputWireBytes);
    Publish(source, EncodeInput(MakeInput(9)));

    bool threw = false;
    try {
        adapter.process_work();
    } catch (const std::length_error&) {
        threw = true;
    }
    Require(threw, "算法超限输出没有 Fail-Fast");
    Require(sink.available() == 0, "超限输出被错误发布");
}

} // namespace

int main() {
    TestFixedPodCompatibility();
    TestVectorCodecAndAlgorithm();
    TestLimitsAreRequired();
    TestEveryByteSplit();
    TestMalformedPayloadAndOversize();
    TestVariableLengthsAndWrap();
    TestDropAndBackpressure();
    TestOutputLimit();
    std::cout << "qa_algorithm_block passed\n";
    return 0;
}
