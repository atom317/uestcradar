#include "ringbuf.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <unistd.h>

namespace {

bool check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "ringbuf-test: " << message << '\n';
    }
    return condition;
}

}  // namespace

int main() {
    const std::string name =
        "/uestcradar_slot_test_" + std::to_string(::getpid());
    RingBuffer* ring = nullptr;
    try {
        ring = ringbuf_create(name.c_str(), {3, 128, 0x42, 7});
        RingWriteLease write;
        RingReadLease read;
        bool ok =
            check(ringbuf_acquire(ring, read) ==
                      RingResult::would_block,
                  "empty ring was readable") &&
            check(ringbuf_reserve(ring, write) == RingResult::ok,
                  "first slot was not reservable") &&
            check(ringbuf_acquire(ring, read) ==
                      RingResult::would_block,
                  "uncommitted slot was visible") &&
            check(ringbuf_commit(write, 129) ==
                      RingResult::invalid_argument,
                  "oversized record was accepted");
        ringbuf_cancel(write);
        ok = ok &&
             check(ringbuf_reserve(ring, write) == RingResult::ok,
                   "cancelled slot was not reusable");
        std::array<std::byte, 16> expected{};
        for (std::size_t index = 0; index < expected.size(); ++index) {
            expected[index] = static_cast<std::byte>(index + 1);
        }
        std::copy(
            expected.begin(), expected.end(), write.payload().begin());
        ok = ok &&
             check(ringbuf_commit(write, expected.size()) ==
                       RingResult::ok,
                   "record commit failed") &&
             check(ringbuf_acquire(ring, read) == RingResult::ok,
                   "committed record was not readable") &&
             check(std::equal(
                       expected.begin(),
                       expected.end(),
                       read.payload().begin()),
                   "record payload mismatch");

        for (int index = 0; index < 2; ++index) {
            RingWriteLease additional;
            ok = ok &&
                 check(ringbuf_reserve(ring, additional) ==
                           RingResult::ok,
                       "free slot was not reservable") &&
                 check(ringbuf_commit(additional, 1) ==
                           RingResult::ok,
                       "additional commit failed");
        }
        RingWriteLease full;
        ok = ok &&
             check(ringbuf_reserve(ring, full) ==
                       RingResult::would_block,
                   "full ring accepted another record") &&
             check(ringbuf_release(read) == RingResult::ok,
                   "read release failed") &&
             check(ringbuf_reserve(ring, full) == RingResult::ok,
                   "released slot was not reusable");
        ringbuf_cancel(full);

        RingBuffer* opened = ringbuf_open(name.c_str());
        ok = ok &&
             check(opened->header->type_id == 0x42 &&
                       opened->header->type_version == 7 &&
                       ringbuf_slot_count(opened) == 3 &&
                       ringbuf_max_payload_bytes(opened) == 128,
                   "cross-process ABI metadata mismatch");
        ringbuf_close(opened);

        ringbuf_shutdown(ring);
        ringbuf_close(ring);
        ringbuf_unlink(name.c_str());
        return ok ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "ringbuf-test: " << error.what() << '\n';
        ringbuf_shutdown(ring);
        ringbuf_close(ring);
        ringbuf_unlink(name.c_str());
        return 1;
    }
}
