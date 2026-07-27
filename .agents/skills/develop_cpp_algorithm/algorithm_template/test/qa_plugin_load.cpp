#include "codec.h"

#include <block_test_harness.h>
#include <cycore_algorithm_sdk.h>
#include <flowgraph/plugin.h>
#include <flowgraph/value.h>

#include <dlfcn.h>

#include <cmath>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <typeindex>
#include <vector>

namespace data = cycore::algorithm::my_block;
namespace fg = cy::flowgraph;
namespace sdk = cycore::sdk;
namespace test_support = cycore::sdk::test;

namespace {

class DlHandle {
public:
    explicit DlHandle(const char* path) : handle_(dlopen(path, RTLD_NOW | RTLD_LOCAL)) {
        if (!handle_) {
            throw std::runtime_error(std::string("dlopen failed: ") + dlerror());
        }
    }
    DlHandle(const DlHandle&) = delete;
    DlHandle& operator=(const DlHandle&) = delete;
    ~DlHandle() {
        if (handle_) {
            dlclose(handle_);
        }
    }

    template <typename Function>
    Function symbol(const char* name) const {
        dlerror();
        void* raw = dlsym(handle_, name);
        if (const char* error = dlerror()) {
            throw std::runtime_error(std::string("dlsym failed for ") + name +
                                     ": " + error);
        }
        return reinterpret_cast<Function>(raw);
    }

private:
    void* handle_ = nullptr;
};

void Require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void VerifyBytePort(const fg::PortBase& port,
                    fg::PortDirection direction,
                    const char* expected_name) {
    Require(port.name() == expected_name, "plugin port name mismatch");
    Require(port.direction() == direction, "plugin port direction mismatch");
    Require(port.port_type() == fg::PortType::STREAM, "plugin port must be a stream");
    Require(port.type_info() == std::type_index(typeid(std::byte)),
            "plugin physical port must be std::byte");
    Require(port.item_size() == sizeof(std::byte), "plugin byte item size mismatch");
}

std::vector<std::byte> MakeInputFrame(data::InputData& input) {
    input.sample_count = 16;
    for (std::size_t i = 0; i < input.sample_count; ++i) {
        input.samples[i] = static_cast<float>(i) * 0.125F;
    }
    const std::size_t payload_bytes = sdk::FrameCodec<data::InputData>::encoded_size(input);
    std::vector<std::byte> payload(payload_bytes);
    Require(sdk::FrameCodec<data::InputData>::encode(
                input, cy::common::Span<std::byte>(payload.data(), payload.size())),
            "input Codec encode failed");
    std::vector<std::byte> wire(sdk::WireFrameBytes(payload.size()));
    Require(sdk::EncodeFrame(
                cy::common::Span<const std::byte>(payload.data(), payload.size()),
                {42, 9876},
                cy::common::Span<std::byte>(wire.data(), wire.size())),
            "input frame encode failed");
    return wire;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: qa_plugin_load /absolute/path/to/my_plugin.so\n";
        return 2;
    }

    DlHandle library(argv[1]);
    const auto make_plugin = library.symbol<fg::PluginMakeFn>("cy_plugin_make");
    const auto free_plugin = library.symbol<fg::PluginFreeFn>("cy_plugin_free");
    std::unique_ptr<fg::Plugin, fg::PluginFreeFn> plugin(make_plugin(), free_plugin);
    Require(static_cast<bool>(plugin), "cy_plugin_make returned null");
    Require(plugin->abi_version() == fg::CY_FLOWGRAPH_PLUGIN_ABI_VERSION,
            "plugin ABI version mismatch");
    Require(std::string(plugin->metadata().name) == "my_plugin",
            "plugin metadata name mismatch");
    Require(plugin->block_registry().contains("algorithm.my_block"),
            "expected Block key is not registered");

    fg::ValueMap params;
    params["factor"] = 3.0;
    params["max_input_frame_bytes"] = std::int64_t{8192};
    params["max_output_frame_bytes"] = std::int64_t{8192};
    auto block = plugin->block_registry().create_block(
        "algorithm.my_block", "plugin_algorithm", params);
    Require(block->input_ports().size() == 1, "plugin must expose one input");
    Require(block->output_ports().size() == 1, "plugin must expose one output");
    VerifyBytePort(*block->input_ports().front(), fg::PortDirection::INPUT, "in");
    VerifyBytePort(*block->output_ports().front(), fg::PortDirection::OUTPUT, "out");

    data::InputData input;
    const auto wire = MakeInputFrame(input);
    {
        test_support::BlockTestHarness<std::byte, std::byte> harness(
            std::move(block), wire.size(), wire.size(), wire.size() * 2, wire.size() * 2);
        harness.publish(wire);
        Require(harness.work_once().succeeded, "dynamic frame Block work failed");
        const auto output_wire = harness.drain_one_transaction();
        const auto inspection = sdk::InspectFrame(
            cy::common::Span<const std::byte>(output_wire.data(), output_wire.size()),
            output_wire.size());
        Require(inspection.status == sdk::FrameParseStatus::CompleteFrame,
                "dynamic plugin output frame is invalid");
        Require(inspection.metadata.sequence_id == 42 &&
                    inspection.metadata.timestamp_unix_nano == 9876,
                "dynamic plugin metadata mismatch");
        data::OutputData output;
        Require(sdk::FrameCodec<data::OutputData>::decode(
                    cy::common::Span<const std::byte>(
                        output_wire.data() + sdk::kFrameEnvelopeBytes,
                        inspection.payload_bytes),
                    output),
                "dynamic plugin output Codec failed");
        for (std::size_t i = 0; i < output.sample_count; ++i) {
            Require(std::abs(output.samples[i] - input.samples[i] * 3.0F) < 1.0e-6F,
                    "dynamic plugin output mismatch");
        }
    }

    plugin.reset();
    std::cout << "qa_plugin_load passed\n";
    return 0;
}
