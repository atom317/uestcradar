#include "data.h"

#include <block_test_harness.h>
#include <flowgraph/plugin.h>
#include <flowgraph/value.h>

#include <cstddef>
#include <cstdint>
#include <dlfcn.h>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <typeindex>
#include <vector>

namespace fg = cy::flowgraph;
namespace data = cycore::algorithm::pulse_compression;
namespace test_support = cycore::sdk::test;

namespace {

class DlHandle {
public:
    explicit DlHandle(const char* path) : handle_(dlopen(path, RTLD_NOW | RTLD_LOCAL)) {
        if (!handle_) throw std::runtime_error(std::string("dlopen failed: ") + dlerror());
    }
    ~DlHandle() { if (handle_) dlclose(handle_); }
    DlHandle(const DlHandle&) = delete;
    DlHandle& operator=(const DlHandle&) = delete;

    template <typename Function>
    Function symbol(const char* name) const {
        dlerror();
        void* result = dlsym(handle_, name);
        if (const char* error = dlerror()) {
            throw std::runtime_error(std::string("dlsym failed for ") + name + ": " + error);
        }
        return reinterpret_cast<Function>(result);
    }

private:
    void* handle_ = nullptr;
};

void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

template <typename Sample>
void VerifyPort(const fg::PortBase& port, fg::PortDirection direction, const char* name) {
    Require(port.name() == name, "plugin port name mismatch");
    Require(port.direction() == direction && port.port_type() == fg::PortType::STREAM,
            "plugin port direction/type mismatch");
    Require(port.type_info() == std::type_index(typeid(Sample)) &&
                port.item_size() == sizeof(Sample) &&
                port.item_alignment() == alignof(Sample),
            "plugin port sample contract mismatch");
}

} // namespace

int main(int argc, char** argv) {
    try {
        Require(argc == 2,
                "usage: qa_pulse_compression_plugin_load /absolute/path/to/pulse_compression.so");
        DlHandle library(argv[1]);
        const auto make_plugin = library.symbol<fg::PluginMakeFn>("cy_plugin_make");
        const auto free_plugin = library.symbol<fg::PluginFreeFn>("cy_plugin_free");
        std::unique_ptr<fg::Plugin, fg::PluginFreeFn> plugin(make_plugin(), free_plugin);

        Require(plugin != nullptr, "cy_plugin_make returned null");
        Require(plugin->abi_version() == fg::CY_FLOWGRAPH_PLUGIN_ABI_VERSION,
                "plugin ABI version mismatch");
        Require(std::string(plugin->metadata().name) == "pulse_compression",
                "plugin metadata name mismatch");
        Require(plugin->block_registry().contains("algorithm.pulse_compression"),
                "Pulse-Compression block key is not registered");

        constexpr std::size_t kChannels = 1;
        constexpr std::size_t kPulses = 1;
        constexpr std::size_t kSamples = 256;
        constexpr std::size_t kElements = kChannels * kPulses * kSamples;
        fg::ValueMap params;
        params["num_channels"] = static_cast<std::int64_t>(kChannels);
        params["num_pulses"] = static_cast<std::int64_t>(kPulses);
        params["samples_per_pulse"] = static_cast<std::int64_t>(kSamples);
        auto block = plugin->block_registry().create_block(
            "algorithm.pulse_compression", "dynamic_pulse_compression", params);
        Require(block->type_name() == "algorithm.pulse_compression", "dynamic block type mismatch");
        Require(block->input_ports().size() == 1 && block->output_ports().size() == 1,
                "Pulse-Compression must expose one input and one output");
        VerifyPort<data::InputSample>(*block->input_ports().front(), fg::PortDirection::INPUT, "in");
        VerifyPort<data::OutputSample>(*block->output_ports().front(), fg::PortDirection::OUTPUT, "out");

        test_support::BlockTestHarness<data::InputSample, data::OutputSample> harness(
            std::move(block), kElements, kElements, kElements * 2, kElements * 2);
        std::vector<data::InputSample> input(kElements, data::InputSample{0, 0});
        harness.publish(input);
        const auto observation = harness.work_once();
        Require(observation.succeeded && observation.consumed_input_elements == kElements &&
                    observation.produced_output_elements == kElements,
                "dynamic pulse-compression transaction mismatch");
        for (const auto& sample : harness.drain_one_transaction()) {
            Require(sample.i == 0 && sample.q == 0,
                    "zero input must produce zero dynamic pulse-compression output");
        }

        std::cout << "qa_pulse_compression_plugin_load passed\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "qa_pulse_compression_plugin_load failed: " << ex.what() << '\n';
        return 1;
    }
}
