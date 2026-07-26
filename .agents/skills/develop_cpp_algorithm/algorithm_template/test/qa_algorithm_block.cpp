#include "algorithm.h"
#include "block_test_harness.h"
#include "data.h"

#include <cycore_algorithm_sdk.h>
#include <flowgraph/block_wrapper.h>
#include <flowgraph/value.h>

#include <cmath>
#include <cstddef>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fg = cy::flowgraph;
namespace sdk = cycore::sdk;
namespace data = cycore::algorithm::my_block;
namespace test_support = algorithm_template::test;

namespace {

using ProductionBlock =
    sdk::AlgorithmBlockAdapter<MyAlgorithm, data::InputSample, data::OutputSample>;
using Harness = test_support::BlockTestHarness<data::InputSample, data::OutputSample>;

void Require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::unique_ptr<fg::BlockModel> MakeProductionBlock(double factor) {
    fg::ValueMap params;
    params["factor"] = factor;
    return std::unique_ptr<fg::BlockModel>(
        new fg::BlockWrapper<ProductionBlock>(
            "algorithm_under_test", fg::BlockTypeName{"algorithm.my_block"}, params));
}

std::vector<data::InputSample> MakeInput(float offset = 0.0f) {
    std::vector<data::InputSample> input(data::kInputElementsPerWork);
    for (std::size_t i = 0; i < input.size(); ++i) {
        input[i] = offset + static_cast<float>(i) * 0.25f;
    }
    return input;
}

void VerifyScaled(const std::vector<data::InputSample>& input,
                  const std::vector<data::OutputSample>& output,
                  float factor) {
    Require(output.size() == data::kOutputElementsPerWork,
            "output transaction size mismatch");
    Require(input.size() == output.size(), "template algorithm expects equal input/output size");
    for (std::size_t i = 0; i < output.size(); ++i) {
        const float expected = input[i] * factor;
        if (std::abs(output[i] - expected) > 1.0e-6f) {
            throw std::runtime_error("scaled output mismatch at element " + std::to_string(i));
        }
    }
}

void TestSuccessfulTransaction() {
    constexpr float kFactor = 2.0f;
    Harness harness(
        MakeProductionBlock(kFactor),
        data::kInputElementsPerWork,
        data::kOutputElementsPerWork,
        data::kInputElementsPerWork * 2,
        data::kOutputElementsPerWork * 2);

    const auto input = MakeInput();
    harness.publish(input);
    const auto observation = harness.work_once();
    Require(observation.succeeded, "complete input should produce one successful work");
    Require(observation.consumed_input_elements == data::kInputElementsPerWork,
            "successful work consumed the wrong input count");
    Require(observation.produced_output_elements == data::kOutputElementsPerWork,
            "successful work produced the wrong output count");
    VerifyScaled(input, harness.drain_one_transaction(), kFactor);
}

void TestInsufficientInputRollback() {
    Harness harness(
        MakeProductionBlock(1.0),
        data::kInputElementsPerWork,
        data::kOutputElementsPerWork,
        data::kInputElementsPerWork * 2,
        data::kOutputElementsPerWork * 2);

    std::vector<data::InputSample> partial(data::kInputElementsPerWork - 1, 1.0f);
    harness.publish(partial);
    const std::size_t input_before = harness.input_available();
    const auto observation = harness.work_once();

    Require(!observation.succeeded, "insufficient input must not report success");
    Require(observation.consumed_input_elements == 0,
            "insufficient input must not be consumed");
    Require(observation.produced_output_elements == 0,
            "insufficient input must not produce output");
    Require(harness.input_available() == input_before,
            "insufficient input changed ring state");
    Require(harness.output_available() == 0,
            "insufficient input unexpectedly produced output");
}

void TestOutputBackpressureRollback() {
    Harness harness(
        MakeProductionBlock(1.0),
        data::kInputElementsPerWork,
        data::kOutputElementsPerWork,
        data::kInputElementsPerWork * 2,
        data::kOutputElementsPerWork);

    const auto first = MakeInput();
    harness.publish(first);
    Require(harness.work_once().succeeded, "first work should fill the output ring");
    Require(harness.output_writable() == 0, "output ring should be full");

    const auto second = MakeInput(1000.0f);
    harness.publish(second);
    const std::size_t input_before = harness.input_available();
    const std::size_t output_before = harness.output_available();
    const auto blocked = harness.work_once();

    Require(!blocked.succeeded, "full output ring must apply backpressure");
    Require(blocked.consumed_input_elements == 0,
            "Reader input must roll back when Writer cannot reserve output");
    Require(blocked.produced_output_elements == 0,
            "backpressured work must not commit partial output");
    Require(harness.input_available() == input_before,
            "backpressured work changed input availability");
    Require(harness.output_available() == output_before,
            "backpressured work changed output availability");

    VerifyScaled(first, harness.drain_one_transaction(), 1.0f);
    Require(harness.work_once().succeeded,
            "rolled-back input should succeed after output is drained");
    VerifyScaled(second, harness.drain_one_transaction(), 1.0f);
}

void TestRingWrapAcrossTransactions() {
    Harness harness(
        MakeProductionBlock(0.5),
        data::kInputElementsPerWork,
        data::kOutputElementsPerWork,
        data::kInputElementsPerWork * 2,
        data::kOutputElementsPerWork * 2);

    for (std::size_t iteration = 0; iteration < 7; ++iteration) {
        const auto input = MakeInput(static_cast<float>(iteration) * 100.0f);
        harness.publish(input);
        Require(harness.work_once().succeeded,
                "aligned ring transaction failed while cursors wrapped");
        VerifyScaled(input, harness.drain_one_transaction(), 0.5f);
    }
}

} // namespace

int main() {
    TestSuccessfulTransaction();
    TestInsufficientInputRollback();
    TestOutputBackpressureRollback();
    TestRingWrapAcrossTransactions();
    std::cout << "qa_algorithm_block passed\n";
    return 0;
}
