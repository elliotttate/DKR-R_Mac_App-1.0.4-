#include "datagram_socket.hpp"

#include <cassert>
#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>

int main() {
    using namespace dkr::runtime::netplay;
    DatagramSocket first;
    DatagramSocket second;
    std::string error;
    assert(first.open(0U, error));
    assert(second.open(0U, error));
    PeerAddress destination{};
    assert(DatagramSocket::resolve("127.0.0.1", second.local_port(), destination, error));
    const std::vector<std::uint8_t> payload{1U, 2U, 3U, 4U};
    assert(first.send(destination, payload, error));
    PeerAddress source{};
    std::vector<std::uint8_t> received;
    for (int attempt = 0; attempt < 100 && received.empty(); ++attempt) {
        second.receive(source, received, error);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(error.empty());
    assert(received == payload);
    assert(!DatagramSocket::describe(source).empty());

    // A retired/background DKR-R process must never share the visible host's
    // port and consume invitations nondeterministically.
    const std::uint16_t owned_port = first.local_port();
    second.close();
    DatagramSocket duplicate_host;
    assert(!duplicate_host.open(owned_port, error));
    assert(!error.empty());
}
