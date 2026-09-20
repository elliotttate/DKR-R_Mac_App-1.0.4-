#pragma once

namespace dkr::runtime::netplay {
// In the pinned SDK, false on a connected SCTP stream means queued, not rejected.
// A route which closed/retired during the call is instead repaired by the caller.
// Acknowledged authority/control protocols remain responsible for delivery.
constexpr bool quick_join_send_accepted(bool sent_immediately,
                                         bool channel_open_after,
                                         bool connection_connected_after,
                                         bool current_route_after) {
    return sent_immediately ||
        (channel_open_after && connection_connected_after && current_route_after);
}
} // namespace dkr::runtime::netplay
