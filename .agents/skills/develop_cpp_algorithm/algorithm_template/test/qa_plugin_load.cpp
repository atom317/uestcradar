#include "block_test_harness.h"
#include "data.h"

#include <flowgraph/plugin.h>
#include <flowgraph/value.h>

#include <dlfcn.h>

#include <cmath>
#include <cstddef>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <typeindex>
#include <vector>

namespace fg = cy::flowgraph;
namespace data = cycore::algorithm::my_block;
namespace test_support = algorithm_template::test;

namespace {

class DlHandle {
public:
    explicit DlHandle(const char* path)
        : handle_(dlopen(path, RTLD_NOW | RTLD_LOCAL)) {
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
            throw std::runtime_error(
                std::string("dlsym failed for ") + name + ": " + error);
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

template <typename Sample>
void VerifyPort(const fg::PortBase& port,
                fg::PortDirection direction,
                const char* expected_name) {
    Require(port.name() == expected_name, "plugin port name mismatch");
    Require(port.direction() == direction, "plugin port direction mismatch");
    Require(port.port_type() == fg::PortType::STREAM, "plugin port must be a stream");
    Require(port.type_info() == std::type_index(typeid(Sample)),
            "plugin port C++ sample type mismatch");
    Require(port.item_size() == sizeof(Sample), "plugin port item size mismatch");
    Require(port.item_alignment() == alignof(Sample),
            "plugin port item alignment mismatch");
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
    auto block = plugin->block_registry().create_block(
        "algorithm.my_block", "plugin_algorithm", params);
    Require(block->type_name() == "algorithm.my_block", "dynamic Block type mismatch");
    Require(block->input_ports().size() == 1, "plugin must expose one input");
    Require(block->output_ports().size() == 1, "plugin must expose one output");
    VerifyPort<data::InputSample>(
        *block->input_ports().front(), fg::PortDirection::INPUT, "in");
    VerifyPort<data::OutputSample>(
        *block->output_ports().front(), fg::PortDirection::OUTPUT, "out");

    {
        test_support::BlockTestHarness<data::InputSample, data::OutputSample> harness(
            std::move(block),
            data::kInputElementsPerWork,
            data::kOutputElementsPerWork,
            data::kInputElementsPerWork * 2,
            data::kOutputElementsPerWork * 2);

        std::vector<data::InputSample> input(data::kInputElementsPerWork);
        for (std::size_t i = 0; i < input.size(); ++i) {
            input[i] = static_cast<float>(i) * 0.125f;
        }
        harness.publish(input);
        const auto observation = harness.work_once();
        Require(observation.succeeded, "dynamic plugin Block work failed");
        const auto output = harness.drain_one_transaction();
        Require(output.size() == data::kOutputElementsPerWork,
                "dynamic plugin output size mismatch");
        for (std::size_t i = 0; i < output.size(); ++i) {
            if (std::abs(output[i] - input[i] * 3.0f) > 1.0e-6f) {
                throw std::runtime_error(
                    "dynamic plugin output mismatch at element " + std::to_string(i));
            }
        }
    }

    plugin.reset();
    std::cout << "qa_plugin_load passed\n";
    return 0;
}
