#pragma once

#include <flowgraph/block_model.h>
#include <flowgraph/port.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <typeindex>
#include <utility>
#include <vector>

namespace algorithm_template::test {

struct WorkObservation {
    bool succeeded = false;
    std::size_t consumed_input_elements = 0;
    std::size_t produced_output_elements = 0;
    std::chrono::nanoseconds latency{0};
};

template <typename InputSample, typename OutputSample>
class BlockTestHarness {
    static_assert(std::is_trivially_copyable<InputSample>::value,
                  "InputSample must be trivially copyable");
    static_assert(std::is_trivially_copyable<OutputSample>::value,
                  "OutputSample must be trivially copyable");

public:
    BlockTestHarness(std::unique_ptr<cy::flowgraph::BlockModel> block,
                     std::size_t input_elements_per_work,
                     std::size_t output_elements_per_work,
                     std::size_t input_capacity,
                     std::size_t output_capacity)
        : block_(std::move(block)),
          input_elements_per_work_(input_elements_per_work),
          output_elements_per_work_(output_elements_per_work) {
        if (!block_) {
            throw std::invalid_argument("BlockTestHarness requires a BlockModel");
        }
        if (input_elements_per_work_ == 0 || output_elements_per_work_ == 0) {
            throw std::invalid_argument(
                "BlockTestHarness v1 requires positive fixed input and output transaction sizes");
        }
        if (input_capacity == 0 || output_capacity == 0) {
            throw std::invalid_argument("Ring buffer capacities must be positive");
        }
        if (block_->input_ports().size() != 1 || block_->output_ports().size() != 1) {
            throw std::invalid_argument(
                "BlockTestHarness v1 supports exactly one input and one output port");
        }

        input_port_ = block_->input_ports().front();
        output_port_ = block_->output_ports().front();
        validate_port<InputSample>(*input_port_, cy::flowgraph::PortDirection::INPUT);
        validate_port<OutputSample>(*output_port_, cy::flowgraph::PortDirection::OUTPUT);

        source_.connect_to(*input_port_, input_capacity);
        output_port_->connect_to(sink_, output_capacity);
        block_->init();
        block_->start();
        started_ = true;
    }

    BlockTestHarness(const BlockTestHarness&) = delete;
    BlockTestHarness& operator=(const BlockTestHarness&) = delete;

    ~BlockTestHarness() {
        if (started_) {
            try {
                block_->stop();
            } catch (...) {
                // Test cleanup must not mask the original failure.
            }
        }
    }

    void publish(const InputSample* data, std::size_t count) {
        if (count == 0) {
            throw std::invalid_argument("Cannot publish an empty input");
        }
        if (!data) {
            throw std::invalid_argument("Input data pointer must not be null");
        }
        auto span = source_.reserve(count);
        if (span.size() != count) {
            throw std::runtime_error("Input ring could not reserve one contiguous publication");
        }
        std::copy(data, data + count, span.data());
        span.commit(count);
    }

    void publish(const std::vector<InputSample>& data) {
        publish(data.data(), data.size());
    }

    WorkObservation work_once() {
        const std::size_t input_before = input_available();
        const std::size_t output_before = output_available();

        const auto begin = std::chrono::steady_clock::now();
        block_->work();
        const auto end = std::chrono::steady_clock::now();

        const std::size_t input_after = input_available();
        const std::size_t output_after = output_available();
        if (input_after > input_before || output_after < output_before) {
            throw std::runtime_error("Block changed port availability in an unsupported direction");
        }

        WorkObservation observation;
        observation.consumed_input_elements = input_before - input_after;
        observation.produced_output_elements = output_after - output_before;
        observation.latency =
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin);

        const bool complete_success =
            observation.consumed_input_elements == input_elements_per_work_ &&
            observation.produced_output_elements == output_elements_per_work_;
        const bool complete_rollback =
            observation.consumed_input_elements == 0 &&
            observation.produced_output_elements == 0;
        if (!complete_success && !complete_rollback) {
            throw std::runtime_error(
                "Block violated the fixed transactional consume/produce contract");
        }
        observation.succeeded = complete_success;
        return observation;
    }

    std::vector<OutputSample> drain(std::size_t count) {
        if (count == 0) {
            throw std::invalid_argument("Cannot drain an empty output");
        }
        if (sink_.available() < count) {
            throw std::runtime_error("Output ring does not contain the requested element count");
        }
        auto span = sink_.get(count);
        if (span.size() != count) {
            throw std::runtime_error("Output ring could not expose one contiguous transaction");
        }
        std::vector<OutputSample> result(span.data(), span.data() + span.size());
        span.consume(span.size());
        return result;
    }

    std::vector<OutputSample> drain_one_transaction() {
        return drain(output_elements_per_work_);
    }

    std::size_t input_available() const {
        return input_port_->dynamic_port().available();
    }

    std::size_t output_available() const {
        return sink_.available();
    }

    std::size_t output_writable() const {
        return output_port_->dynamic_port().available();
    }

    cy::flowgraph::BlockModel& block() noexcept {
        return *block_;
    }

private:
    template <typename Sample>
    static void validate_port(cy::flowgraph::PortBase& port,
                              cy::flowgraph::PortDirection expected_direction) {
        const auto dynamic = port.dynamic_port();
        if (dynamic.direction() != expected_direction ||
            dynamic.port_type() != cy::flowgraph::PortType::STREAM) {
            throw std::invalid_argument("Block port direction or kind does not match the harness");
        }
        if (dynamic.type_info() != std::type_index(typeid(Sample)) ||
            dynamic.item_size() != sizeof(Sample) ||
            dynamic.item_alignment() != alignof(Sample)) {
            throw std::invalid_argument("Block port sample type does not match the harness");
        }
    }

    // Declaration order keeps the connected reader/block alive until their
    // corresponding writers are destroyed.
    cy::flowgraph::PortOut<InputSample> source_;
    std::unique_ptr<cy::flowgraph::BlockModel> block_;
    cy::flowgraph::PortIn<OutputSample> sink_;
    cy::flowgraph::PortBase* input_port_ = nullptr;
    cy::flowgraph::PortBase* output_port_ = nullptr;
    std::size_t input_elements_per_work_ = 0;
    std::size_t output_elements_per_work_ = 0;
    bool started_ = false;
};

} // namespace algorithm_template::test
