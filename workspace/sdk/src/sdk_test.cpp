#include <data.h>
#include <sdk.h>

#include "ringbuf/ringbuf.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <unistd.h>

namespace {

class OwnedRing {
public:
    OwnedRing(
        std::string name,
        std::uint64_t type_id,
        std::uint32_t slots = 2)
        : name_(std::move(name)),
          ring_(ringbuf_create(
              name_.c_str(),
              {slots, 4096, type_id, 1})) {}

    ~OwnedRing() {
        ringbuf_shutdown(ring_);
        ringbuf_close(ring_);
        ringbuf_unlink(name_.c_str());
    }

    RingBuffer* get() const noexcept {
        return ring_;
    }

    const std::string& name() const noexcept {
        return name_;
    }

private:
    std::string name_;
    RingBuffer* ring_;
};

template <class Metadata, class Sample>
std::vector<std::byte> make_payload(
    const Metadata& metadata,
    const std::vector<Sample>& samples) {
    std::vector<std::byte> result(
        sizeof(metadata) + samples.size() * sizeof(Sample));
    std::memcpy(result.data(), &metadata, sizeof(metadata));
    std::memcpy(
        result.data() + sizeof(metadata),
        samples.data(),
        samples.size() * sizeof(Sample));
    return result;
}

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void test_input_lifetime(const std::string& prefix) {
    OwnedRing upstream{prefix + "_iq_up", 1};
    ::setenv(
        "UESTCRADAR_UPSTREAM_SHM_NAME",
        upstream.name().c_str(),
        1);

    const uestcradar::IQMetadata metadata{
        42,
        123456,
        2,
        3,
        2.5e6,
        1.2e9,
    };
    std::vector<uestcradar::ComplexInt16> samples{
        {1, -1}, {2, -2}, {3, -3},
        {4, -4}, {5, -5}, {6, -6},
    };
    const auto payload = make_payload(metadata, samples);
    require(
        ringbuf_write(
            upstream.get(), payload.data(), payload.size()) ==
            static_cast<std::int32_t>(payload.size()),
        "could not seed first IQ frame");
    require(
        ringbuf_write(
            upstream.get(), payload.data(), payload.size()) ==
            static_cast<std::int32_t>(payload.size()),
        "could not seed second IQ frame");

    uestcradar::Input<uestcradar::IQFrame> input;
    {
        auto frame = input.read();
        require(
            frame.metadata.frame_id == 42 &&
                frame.data.rows() == 2 &&
                frame.data.columns() == 3 &&
                frame.data[1][2].i == 6 &&
                frame.data[1][2].q == -6,
            "IQ frame mapping is incorrect");
        require(
            frame.data[1].data() ==
                frame.data[0].data() + 3,
            "IQ rows are not contiguous");

        RingWriteLease blocked;
        require(
            ringbuf_reserve(upstream.get(), blocked) ==
                RingResult::would_block,
            "input Slot was reused while frame was alive");
    }

    RingWriteLease reusable;
    require(
        ringbuf_reserve(upstream.get(), reusable) ==
            RingResult::ok,
        "input Slot was not released by frame destructor");
    ringbuf_cancel(reusable);
}

void test_output_commit_and_cancel(const std::string& prefix) {
    OwnedRing downstream{prefix + "_pc_down", 2};
    ::setenv(
        "UESTCRADAR_DOWNSTREAM_SHM_NAME",
        downstream.name().c_str(),
        1);

    uestcradar::Output<uestcradar::PulseCompressionFrame> output;
    const uestcradar::PulseCompressionMetadata metadata{
        .frame_id = 9,
        .timestamp_unix_ns = 987654,
        .channel_count = 2,
        .range_bin_count = 3,
        .pulse_index = 7,
        .pulses_per_cpi = 128,
        .range_resolution_m = 1.5,
    };
    {
        auto frame = output.create(metadata);
        for (std::size_t row = 0; row < frame.data.rows(); ++row) {
            for (std::size_t column = 0;
                 column < frame.data.columns();
                 ++column) {
                frame.data[row][column] = {
                    static_cast<float>(row * 10 + column),
                    -static_cast<float>(row * 10 + column),
                };
            }
        }
        RingReadLease hidden;
        require(
            ringbuf_acquire(downstream.get(), hidden) ==
                RingResult::would_block,
            "uncommitted output frame became visible");
        output.write(frame);
    }

    RingReadLease committed;
    require(
        ringbuf_acquire(downstream.get(), committed) ==
            RingResult::ok,
        "committed output frame is not readable");
    const std::size_t expected_length =
        sizeof(metadata) +
        6 * sizeof(uestcradar::ComplexFloat32);
    require(
        committed.payload().size() == expected_length,
        "committed output length is incorrect");
    uestcradar::PulseCompressionMetadata actual_metadata{};
    std::memcpy(
        &actual_metadata,
        committed.payload().data(),
        sizeof(actual_metadata));
    require(
        actual_metadata.frame_id == metadata.frame_id &&
            actual_metadata.range_bin_count == 3,
        "output metadata is incorrect");
    require(
        ringbuf_release(committed) == RingResult::ok,
        "could not release committed frame");

    {
        auto abandoned = output.create(metadata);
        abandoned.data[0][0] = {3.0F, 4.0F};
    }
    RingWriteLease reusable;
    require(
        ringbuf_reserve(downstream.get(), reusable) ==
            RingResult::ok,
        "abandoned output Slot was not reusable");
    ringbuf_cancel(reusable);
}

void test_rd_layout_and_type_mismatch(const std::string& prefix) {
    std::array<float, 12> values{};
    uestcradar::Array2D<float> matrix{values.data(), 3, 4};
    matrix[2][3] = 17.0F;
    require(
        matrix.rows() == 3 &&
            matrix.columns() == 4 &&
            values[11] == 17.0F,
        "RD row-major Array2D mapping is incorrect");

    OwnedRing wrong_type{prefix + "_wrong_up", 3};
    ::setenv(
        "UESTCRADAR_UPSTREAM_SHM_NAME",
        wrong_type.name().c_str(),
        1);
    bool rejected = false;
    try {
        uestcradar::Input<uestcradar::IQFrame> input;
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "frame type mismatch was not rejected");
}

}  // namespace

int main() {
    try {
        const std::string prefix =
            "/uestcradar_sdk_test_" + std::to_string(::getpid());
        test_input_lifetime(prefix);
        test_output_commit_and_cancel(prefix);
        test_rd_layout_and_type_mismatch(prefix);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "sdk-test: " << error.what() << '\n';
        return 1;
    }
}
