#include "telemetry.hpp"

#include "ringbuf/ringbuf.hpp"
#include "telemetry.pb.h"

#include <array>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <memory>
#include <string>
#include <system_error>
#include <thread>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>

namespace {

constexpr std::size_t kMaxDatagramSize = 1400;
constexpr unsigned long kDefaultIntervalMs = 100;
constexpr char kDefaultHost[] = "telemetry-web";
constexpr char kDefaultPort[] = "9900";
constexpr char kDefaultNodeId[] = "local";

struct RingSnapshot {
    std::uint64_t capacity;
    std::uint64_t used;
    std::uint64_t write_position;
    std::uint64_t read_position;
    bool shutdown;
};

[[noreturn]] void throw_system_error(const char* operation) {
    throw std::system_error(errno, std::generic_category(), operation);
}

std::string environment_or(const char* name, const char* fallback) {
    const char* value = std::getenv(name);
    return value != nullptr && value[0] != '\0' ? value : fallback;
}

std::chrono::milliseconds sample_interval() {
    const std::string value =
        environment_or("SAMPLE_INTERVAL", "100");
    char* end = nullptr;
    errno = 0;
    const unsigned long milliseconds = std::strtoul(value.c_str(), &end, 10);
    if (errno != 0 || end == value.c_str() || *end != '\0' ||
        milliseconds < 10 ||
        milliseconds > static_cast<unsigned long>(
                           std::numeric_limits<int>::max())) {
        return std::chrono::milliseconds{kDefaultIntervalMs};
    }
    return std::chrono::milliseconds{milliseconds};
}

class RingObserver {
public:
    explicit RingObserver(const char* name) {
        fd_ = ::shm_open(name, O_RDONLY | O_CLOEXEC, 0);
        if (fd_ == -1) {
            throw_system_error("shm_open(observer)");
        }

        struct stat status {};
        if (::fstat(fd_, &status) == -1) {
            const int saved_errno = errno;
            ::close(fd_);
            errno = saved_errno;
            throw_system_error("fstat(observer)");
        }
        if (status.st_size < static_cast<off_t>(kRingHeaderSize)) {
            ::close(fd_);
            errno = EPROTO;
            throw_system_error("ringbuf header size");
        }

        void* address = ::mmap(
            nullptr,
            kRingHeaderSize,
            PROT_READ,
            MAP_SHARED,
            fd_,
            0);
        if (address == MAP_FAILED) {
            const int saved_errno = errno;
            ::close(fd_);
            errno = saved_errno;
            throw_system_error("mmap(observer)");
        }
        header_ = static_cast<const RingBufferHeader*>(address);

        if (header_->magic.load(std::memory_order_acquire) != kRingMagic ||
            header_->abi_version != kRingAbiVersion ||
            header_->header_size != kRingHeaderSize ||
            header_->capacity_bytes == 0) {
            ::munmap(const_cast<RingBufferHeader*>(header_), kRingHeaderSize);
            ::close(fd_);
            header_ = nullptr;
            fd_ = -1;
            errno = EPROTO;
            throw_system_error("ringbuf ABI(observer)");
        }
    }

    RingObserver(const RingObserver&) = delete;
    RingObserver& operator=(const RingObserver&) = delete;

    ~RingObserver() {
        if (header_ != nullptr) {
            ::munmap(const_cast<RingBufferHeader*>(header_), kRingHeaderSize);
        }
        if (fd_ != -1) {
            ::close(fd_);
        }
    }

    [[nodiscard]] bool snapshot(RingSnapshot& output) const noexcept {
        const std::uint64_t capacity = header_->capacity_bytes;
        for (int attempt = 0; attempt < 3; ++attempt) {
            const std::uint64_t read =
                header_->read_position.load(std::memory_order_acquire);
            const std::uint64_t write =
                header_->write_position.load(std::memory_order_acquire);
            if (write >= read && write - read <= capacity) {
                output = RingSnapshot{
                    capacity,
                    write - read,
                    write,
                    read,
                    header_->shutdown.load(std::memory_order_acquire) != 0,
                };
                return true;
            }
        }
        return false;
    }

private:
    int fd_{-1};
    const RingBufferHeader* header_{nullptr};
};

class UdpSender {
public:
    UdpSender(const std::string& host, const std::string& port) {
        struct addrinfo hints {};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_DGRAM;

        struct addrinfo* addresses = nullptr;
        const int result =
            ::getaddrinfo(host.c_str(), port.c_str(), &hints, &addresses);
        if (result != 0) {
            throw std::system_error(
                EHOSTUNREACH,
                std::generic_category(),
                ::gai_strerror(result));
        }
        std::unique_ptr<struct addrinfo, decltype(&::freeaddrinfo)> guard(
            addresses,
            ::freeaddrinfo);

        for (const struct addrinfo* address = addresses;
             address != nullptr;
             address = address->ai_next) {
            const int candidate = ::socket(
                address->ai_family,
                address->ai_socktype | SOCK_NONBLOCK | SOCK_CLOEXEC,
                address->ai_protocol);
            if (candidate == -1) {
                continue;
            }
            if (::connect(candidate, address->ai_addr, address->ai_addrlen) == 0) {
                fd_ = candidate;
                return;
            }
            ::close(candidate);
        }
        throw_system_error("connect(telemetry UDP)");
    }

    UdpSender(const UdpSender&) = delete;
    UdpSender& operator=(const UdpSender&) = delete;

    ~UdpSender() {
        if (fd_ != -1) {
            ::close(fd_);
        }
    }

    void send(const void* data, std::size_t size) const noexcept {
        const ssize_t result =
            ::send(fd_, data, size, MSG_DONTWAIT | MSG_NOSIGNAL);
        static_cast<void>(result);
    }

private:
    int fd_{-1};
};

void append_metric(
    cycomm::telemetry::v1::TelemetryPacket& packet,
    const std::string& node_id,
    const char* link_id,
    std::uint64_t sequence,
    std::uint64_t observed_unix_ns,
    const RingSnapshot& snapshot) {
    auto* metric = packet.add_rings();
    metric->set_node_id(node_id);
    metric->set_link_id(link_id);
    metric->set_sequence(sequence);
    metric->set_observed_unix_ns(observed_unix_ns);
    metric->set_capacity_bytes(snapshot.capacity);
    metric->set_used_bytes(snapshot.used);
    metric->set_write_position(snapshot.write_position);
    metric->set_read_position(snapshot.read_position);
    metric->set_shutdown(snapshot.shutdown);
}

}  // namespace

int run_telemetry_exporter(volatile std::sig_atomic_t& running) {
    const std::string node_id =
        environment_or("NODE_ID", kDefaultNodeId);
    const std::string host =
        environment_or("TELEMETRY_HOST", kDefaultHost);
    const std::string port =
        environment_or("TELEMETRY_PORT", kDefaultPort);
    const std::chrono::milliseconds interval = sample_interval();

    RingObserver upstream{kUpstreamBufName};
    RingObserver downstream{kDownstreamBufName};
    UdpSender sender{host, port};
    std::array<std::byte, kMaxDatagramSize> buffer{};
    std::uint64_t sequence = 0;

    while (running != 0) {
        const auto now = std::chrono::system_clock::now();
        const std::uint64_t observed_unix_ns =
            static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    now.time_since_epoch())
                    .count());

        cycomm::telemetry::v1::TelemetryPacket packet;
        RingSnapshot snapshot{};
        ++sequence;
        if (upstream.snapshot(snapshot)) {
            append_metric(
                packet,
                node_id,
                "upstream",
                sequence,
                observed_unix_ns,
                snapshot);
        }
        if (downstream.snapshot(snapshot)) {
            append_metric(
                packet,
                node_id,
                "downstream",
                sequence,
                observed_unix_ns,
                snapshot);
        }

        const std::size_t payload_size = packet.ByteSizeLong();
        if (packet.rings_size() != 0 &&
            payload_size <= buffer.size() &&
            packet.SerializeToArray(
                buffer.data(),
                static_cast<int>(payload_size))) {
            sender.send(buffer.data(), payload_size);
        }

        std::this_thread::sleep_for(interval);
    }
    return 0;
}
