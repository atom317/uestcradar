#include "iq_frame.h"

#include <cycore_algorithm_sdk.h>
#include <cycore_benchmark_harness.h>

#include <dlfcn.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace fg = cy::flowgraph;
namespace sdk = cycore::sdk;
using uestcradar::nodes::IQFrame;

namespace {

void Require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

class Fixture {
public:
    Fixture() {
        const auto id = std::chrono::steady_clock::now()
                            .time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
                ("file_source_sim_sink_" + std::to_string(id) + ".bin");
        values_.resize(30);
        for (std::size_t i = 0; i < values_.size(); ++i) {
            values_[i] = cy::common::CS16{
                static_cast<std::int16_t>(i + 11),
                static_cast<std::int16_t>(-static_cast<std::int32_t>(i + 11))};
        }
        std::ofstream output(path_, std::ios::binary);
        output.write(
            reinterpret_cast<const char*>(values_.data()),
            static_cast<std::streamsize>(
                values_.size() * sizeof(values_.front())));
        Require(output.good(), "failed to create integration fixture");
    }

    ~Fixture() {
        std::error_code error;
        std::filesystem::remove(path_, error);
    }

    std::string path() const { return path_.string(); }
    const std::vector<cy::common::CS16>& values() const { return values_; }

private:
    std::filesystem::path path_;
    std::vector<cy::common::CS16> values_;
};

class PluginHandle {
public:
    explicit PluginHandle(const char* path) {
        handle_ = dlopen(path, RTLD_NOW | RTLD_LOCAL);
        if (handle_ == nullptr) {
            throw std::runtime_error(dlerror());
        }
        make_ = reinterpret_cast<cy::flowgraph::PluginMakeFn>(
            dlsym(handle_, "cy_plugin_make"));
        free_ = reinterpret_cast<cy::flowgraph::PluginFreeFn>(
            dlsym(handle_, "cy_plugin_free"));
        if (make_ == nullptr || free_ == nullptr) {
            dlclose(handle_);
            handle_ = nullptr;
            throw std::runtime_error("plugin entry points are missing");
        }
        plugin_ = make_();
        if (plugin_ == nullptr) {
            throw std::runtime_error("plugin factory returned null");
        }
    }

    ~PluginHandle() {
        if (plugin_ != nullptr) {
            free_(plugin_);
        }
        if (handle_ != nullptr) {
            dlclose(handle_);
        }
    }

    cy::flowgraph::Plugin& plugin() noexcept { return *plugin_; }

private:
    void* handle_ = nullptr;
    cy::flowgraph::PluginMakeFn make_ = nullptr;
    cy::flowgraph::PluginFreeFn free_ = nullptr;
    cy::flowgraph::Plugin* plugin_ = nullptr;
};

class Observer {
public:
    explicit Observer(std::size_t maximum_wire_bytes)
        : wire_(maximum_wire_bytes) {
        frame_.payload.reserve(12);
    }

    bool read(fg::PortIn<std::byte>& input,
              std::size_t maximum_wire_bytes,
              sdk::FrameMetadata* metadata) {
        if (input.available() < sdk::kFrameEnvelopeBytes) {
            return false;
        }
        auto envelope = cy::common::Span<std::byte>(
            wire_.data(), sdk::kFrameEnvelopeBytes);
        Require(input.peek_copy(0, envelope) == envelope.size(),
                "integration envelope peek failed");
        auto inspection = sdk::InspectFrame(
            cy::common::Span<const std::byte>(
                envelope.data(), envelope.size()),
            maximum_wire_bytes);
        if (inspection.wire_bytes == 0 ||
            input.available() < inspection.wire_bytes) {
            return false;
        }
        auto wire = cy::common::Span<std::byte>(
            wire_.data(), inspection.wire_bytes);
        Require(input.peek_copy(0, wire) == wire.size(),
                "integration wire peek failed");
        inspection = sdk::InspectFrame(
            cy::common::Span<const std::byte>(wire.data(), wire.size()),
            maximum_wire_bytes);
        Require(inspection.status == sdk::FrameParseStatus::CompleteFrame,
                "integration observer received invalid frame");
        Require(sdk::FrameDataCodec<IQFrame>::decode(
                    cy::common::Span<const std::byte>(
                        wire.data() + sdk::kFrameEnvelopeBytes,
                        inspection.payload_bytes),
                    frame_),
                "integration observer decode failed");
        Require(input.consume_exact(inspection.wire_bytes),
                "integration observer consume failed");
        *metadata = inspection.metadata;
        return true;
    }

    const IQFrame& frame() const noexcept { return frame_; }

private:
    std::vector<std::byte> wire_;
    IQFrame frame_;
};

void DriveOne(fg::BlockModel& source,
              fg::BlockModel& sink,
              fg::PortIn<std::byte>& observer_input,
              Observer& observer,
              std::size_t maximum_wire_bytes,
              sdk::FrameMetadata* metadata) {
    for (std::size_t attempts = 0; attempts < 128; ++attempts) {
        source.work();
        sink.work();
        if (observer.read(
                observer_input, maximum_wire_bytes, metadata)) {
            while (sink.work()) {
            }
            return;
        }
    }
    throw std::runtime_error("integration graph stalled");
}

void Run(const char* file_source_path, const char* sim_sink_path) {
    Fixture fixture;
    PluginHandle file_source_plugin(file_source_path);
    PluginHandle sim_sink_plugin(sim_sink_path);
    Require(file_source_plugin.plugin().block_registry().contains("file_source"),
            "file_source block is not registered");
    Require(sim_sink_plugin.plugin().block_registry().contains("sim_sink"),
            "sim_sink block is not registered");

    const fg::ValueMap params{
        {"file_path", fixture.path()},
        {"pulses", std::int64_t{4}},
        {"samples_per_pulse", std::int64_t{3}},
        {"initial_sequence_id", std::int64_t{20}},
    };
    auto source =
        file_source_plugin.plugin().block_registry().create_block(
            "file_source", "source", params);
    auto sink =
        sim_sink_plugin.plugin().block_registry().create_block(
            "sim_sink", "sink", params);
    auto* output = source->output_port("out");
    auto* input = sink->input_port("in");
    Require(output != nullptr && input != nullptr,
            "integration plugin port lookup failed");
    Require(output->type_info() == typeid(std::byte) &&
                input->type_info() == typeid(std::byte),
            "integration ports are not std::byte");

    constexpr std::size_t maximum_wire_bytes =
        sdk::kFrameEnvelopeBytes + sizeof(uestcradar::nodes::IQFrameHeader) +
        12 * sizeof(cy::common::CS16);
    output->connect_to(*input, maximum_wire_bytes);
    fg::PortIn<std::byte> observer_input;
    output->connect_to(observer_input, maximum_wire_bytes);
    Observer observer(maximum_wire_bytes);

    source->init();
    sink->init();
    source->start();
    sink->start();

    const std::array<std::uint32_t, 3> pulse_cycle{4, 4, 2};
    std::size_t reference_offset = 0;
    std::uint64_t previous_timestamp = 0;
    sdk::FrameMetadata metadata;
    for (std::size_t index = 0; index < 9; ++index) {
        DriveOne(
            *source, *sink, observer_input, observer,
            maximum_wire_bytes, &metadata);
        const auto& frame = observer.frame();
        Require(frame.header.pulses == pulse_cycle[index % 3],
                "integration variable frame sequence mismatch");
        Require(metadata.sequence_id == 20 + index,
                "integration sequence mismatch");
        Require(metadata.timestamp_unix_nano > previous_timestamp,
                "integration timestamp mismatch");
        previous_timestamp = metadata.timestamp_unix_nano;
        for (std::size_t i = 0; i < frame.payload.size(); ++i) {
            Require(
                frame.payload[i] ==
                    fixture.values()[(reference_offset + i) %
                                     fixture.values().size()],
                "integration payload mismatch");
        }
        reference_offset =
            (reference_offset + frame.payload.size()) % fixture.values().size();
    }
    Require(input->dynamic_port().available() == 0,
            "SimSink did not drain the integration edge");

    cycore::benchmark::detail::begin_allocation_tracking();
    for (std::size_t index = 0; index < 30; ++index) {
        DriveOne(
            *source, *sink, observer_input, observer,
            maximum_wire_bytes, &metadata);
    }
    const std::size_t allocations =
        cycore::benchmark::detail::end_allocation_tracking();
    Require(allocations == 0,
            "integration graph allocated during steady-state work");

    source->stop();
    sink->stop();
    source.reset();
    sink.reset();
}

} // namespace

CYCORE_BENCHMARK_DEFINE_ALLOCATION_OPERATORS()

int main(int argc, char** argv) {
    if (argc != 3) {
        return 2;
    }
    Run(argv[1], argv[2]);
    return 0;
}
