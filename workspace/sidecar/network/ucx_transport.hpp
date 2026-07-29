#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace sidecar::network {

struct EndpointOptions {
    std::string address;
    std::uint16_t port{13337};
    std::chrono::milliseconds timeout{10'000};
};

class UCXRequest {
public:
    // Opaque implementation type; public only so UCX C callbacks can name it.
    struct State;

    UCXRequest() noexcept;
    UCXRequest(UCXRequest&& other) noexcept;
    UCXRequest& operator=(UCXRequest&& other) noexcept;
    UCXRequest(const UCXRequest&) = delete;
    UCXRequest& operator=(const UCXRequest&) = delete;
    ~UCXRequest();

    [[nodiscard]] bool completed() const noexcept;
    [[nodiscard]] std::size_t bytes_transferred() const;

private:
    explicit UCXRequest(std::shared_ptr<State> state) noexcept;

    std::shared_ptr<State> state_;

    friend class UCXTransport;
};

class UCXTransport {
public:
    // Opaque implementation type; public only so UCX C callbacks can name it.
    struct Impl;

    static UCXTransport accept_one(const EndpointOptions& options);
    static UCXTransport connect(const EndpointOptions& options);

    UCXTransport(UCXTransport&& other) noexcept;
    UCXTransport& operator=(UCXTransport&& other) noexcept;
    UCXTransport(const UCXTransport&) = delete;
    UCXTransport& operator=(const UCXTransport&) = delete;
    ~UCXTransport();

    [[nodiscard]] UCXRequest send(
        std::span<const std::byte> buffer,
        std::uint64_t tag);
    [[nodiscard]] UCXRequest receive(
        std::span<std::byte> buffer,
        std::uint64_t tag,
        std::uint64_t tag_mask = UINT64_MAX);

    void progress();
    void wait(
        UCXRequest& request,
        std::chrono::milliseconds timeout = std::chrono::seconds{10});

private:
    explicit UCXTransport(std::shared_ptr<Impl> impl) noexcept;

    std::shared_ptr<Impl> impl_;
};

}  // namespace sidecar::network
