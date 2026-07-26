#include "data.h"

#include <block_test_harness.h>
#include <flowgraph/plugin.h>
#include <flowgraph/value.h>

#include <cmath>
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
namespace data = cycore::algorithm::range_doppler;
namespace test_support = cycore::sdk::test;

namespace {

class DlHandle {
public:
    explicit DlHandle(const char* path) : handle_(dlopen(path, RTLD_NOW | RTLD_LOCAL)) {
        if (!handle_) {
            throw std::runtime_error(std::string("dlopen failed: ") + dlerror());
        }
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
        Require(argc == 2, "usage: qa_range_doppler_plugin_load /absolute/path/to/range_doppler.so");
        DlHandle library(argv[1]);
        const auto make_plugin = library.symbol<fg::PluginMakeFn>("cy_plugin_make");
        const auto free_plugin = library.symbol<fg::PluginFreeFn>("cy_plugin_free");
        std::unique_ptr<fg::Plugin, fg::PluginFreeFn> plugin(make_plugin(), free_plugin);

        Require(plugin != nullptr, "cy_plugin_make returned null");
        Require(plugin->abi_version() == fg::CY_FLOWGRAPH_PLUGIN_ABI_VERSION,
                "plugin ABI version mismatch");
        Require(std::string(plugin->metadata().name) == "range_doppler",
                "plugin metadata name mismatch");
        Require(plugin->block_registry().contains("algorithm.range_doppler"),
                "Range-Doppler block key is not registered");

        constexpr std::size_t kChannels = 2;
        constexpr std::size_t kPulses = 8;
        constexpr std::size_t kSamples = 16;
        constexpr std::size_t kInputCount = kChannels * kPulses * kSamples;
        constexpr std::size_t kOutputCount = kPulses * kSamples;
        fg::ValueMap params;
        params["num_channels"] = static_cast<std::int64_t>(kChannels);
        params["num_pulses"] = static_cast<std::int64_t>(kPulses);
        params["samples_per_pulse"] = static_cast<std::int64_t>(kSamples);
        auto block = plugin->block_registry().create_block(
            "algorithm.range_doppler", "dynamic_range_doppler", params);
        Require(block->type_name() == "algorithm.range_doppler", "dynamic block type mismatch");
        Require(block->input_ports().size() == 1 && block->output_ports().size() == 1,
                "Range-Doppler must expose one input and one output");
        VerifyPort<data::InputSample>(*block->input_ports().front(), fg::PortDirection::INPUT, "in");
        VerifyPort<data::OutputSample>(*block->output_ports().front(), fg::PortDirection::OUTPUT, "out");

        test_support::BlockTestHarness<data::InputSample, data::OutputSample> harness(
            std::move(block), kInputCount, kOutputCount, kInputCount * 2, kOutputCount * 2);
        std::vector<data::InputSample> input(kInputCount, data::InputSample{0, 0});
        for (std::size_t pulse = 0; pulse < kPulses; ++pulse) {
            const std::size_t index = ((pulse * kSamples + 4) * kChannels);
            input[index] = data::InputSample{1000, 0};
        }
        harness.publish(input);
        const auto observation = harness.work_once();
        Require(observation.succeeded && observation.consumed_input_elements == kInputCount &&
                    observation.produced_output_elements == kOutputCount,
                "dynamic Range-Doppler transaction mismatch");
        const auto output = harness.drain_one_transaction();
        const float expected_db = 10.0f * std::log10(static_cast<float>(kPulses * 1000 * 1000));
        Require(std::abs(output[(kPulses / 2) * kSamples + 4] - expected_db) < 0.25f,
                "dynamic Range-Doppler output mismatch");

        std::cout << "qa_range_doppler_plugin_load passed\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "qa_range_doppler_plugin_load failed: " << ex.what() << '\n';
        return 1;
    }
}
