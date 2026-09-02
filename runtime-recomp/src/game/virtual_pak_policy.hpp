#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace dkr::runtime::pak::policy {

constexpr std::uint8_t connected_port_mask(
    bool enabled, const std::array<bool, 4>& assigned,
    const std::array<bool, 4>& connected) {
    if (!enabled) return 0;
    std::uint8_t mask = 1U; // Player 1 storage remains available for keyboard play.
    for (std::size_t port = 1; port < 4; ++port) {
        if (assigned[port] && connected[port]) {
            mask |= static_cast<std::uint8_t>(1U << port);
        }
    }
    return mask;
}

constexpr std::uint8_t rumble_port_mask(
    const std::array<bool, 4>& assigned,
    const std::array<bool, 4>& connected,
    const std::array<bool, 4>& rumble) {
    std::uint8_t mask = 0U;
    for (std::size_t port = 0; port < 4; ++port) {
        if (assigned[port] && connected[port] && rumble[port]) {
            mask |= static_cast<std::uint8_t>(1U << port);
        }
    }
    return mask;
}

constexpr std::uint8_t combined_rumble_mask(
    std::uint8_t retail_mask,
    const std::array<bool, 4>& assigned,
    const std::array<bool, 4>& connected,
    const std::array<bool, 4>& rumble) {
    return static_cast<std::uint8_t>(
        retail_mask | rumble_port_mask(assigned, connected, rumble));
}

} // namespace dkr::runtime::pak::policy
