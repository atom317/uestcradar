#include "ucx_transport.hpp"

#include <array>
#include <csignal>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <unistd.h>
#include <sys/wait.h>

namespace {

using sidecar::network::EndpointOptions;
using sidecar::network::UCXRequest;
using sidecar::network::UCXTransport;

constexpr std::uint64_t kTestTag = 0x51;

int run_server(std::uint16_t port) {
    try {
        UCXTransport transport = UCXTransport::accept_one(
            EndpointOptions{"127.0.0.1", port, std::chrono::seconds{10}});
        std::array<std::byte, 64> input{};
        UCXRequest receive = transport.receive(input, kTestTag);
        transport.wait(receive);
        if (receive.bytes_transferred() != input.size()) {
            return 1;
        }
        UCXRequest send = transport.send(input, kTestTag);
        transport.wait(send);
        return 0;
    } catch (...) {
        return 1;
    }
}

}  // namespace

int main() {
    const std::uint16_t port =
        static_cast<std::uint16_t>(20'000 + (::getpid() % 20'000));
    const pid_t server_pid = ::fork();
    if (server_pid == -1) {
        std::cerr << "fork failed\n";
        return 1;
    }
    if (server_pid == 0) {
        ::_exit(run_server(port));
    }

    try {
        std::this_thread::sleep_for(std::chrono::milliseconds{100});
        UCXTransport transport = UCXTransport::connect(
            EndpointOptions{"127.0.0.1", port, std::chrono::seconds{10}});
        std::array<std::byte, 64> output{};
        for (std::size_t index = 0; index < output.size(); ++index) {
            output[index] = static_cast<std::byte>(index);
        }
        UCXRequest send = transport.send(output, kTestTag);
        transport.wait(send);
        std::array<std::byte, 64> reply{};
        UCXRequest receive = transport.receive(reply, kTestTag);
        transport.wait(receive);
        if (reply != output || receive.bytes_transferred() != reply.size()) {
            throw std::runtime_error("payload mismatch");
        }

        bool empty_rejected = false;
        try {
            std::span<const std::byte> empty;
            static_cast<void>(transport.send(empty, kTestTag));
        } catch (const std::invalid_argument&) {
            empty_rejected = true;
        }
        if (!empty_rejected) {
            throw std::runtime_error("empty send was not rejected");
        }
    } catch (const std::exception& error) {
        std::cerr << "ucx-transport-test: " << error.what() << '\n';
        ::kill(server_pid, SIGTERM);
        static_cast<void>(::waitpid(server_pid, nullptr, 0));
        return 1;
    }

    int status = 0;
    if (::waitpid(server_pid, &status, 0) == -1 ||
        !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        std::cerr << "ucx-transport-test: server failed\n";
        return 1;
    }
    return 0;
}
