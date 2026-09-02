#include "ui_notification_policy.hpp"

#include <cassert>
#include <chrono>

int main() {
    using namespace dkr::runtime::ui_notifications;
    using namespace std::chrono_literals;
    const auto start = std::chrono::steady_clock::time_point{10s};
    Queue queue{2U};
    assert(queue.current(start) == nullptr);

    queue.push(Kind::FriendOnline, "FRIEND ONLINE", "Timber is online.", start);
    queue.push(Kind::LobbyInvite, "LOBBY INVITE", "Pipsy invited you.",
               start + 1s);
    assert(queue.size(start + 1s) == 2U);
    assert(queue.current(start + 1s)->kind == Kind::FriendOnline);
    assert(Queue::alpha(*queue.current(start + 1s), start + 1s) == 1.0F);

    const Toast* second = queue.current(start + 7s);
    assert(second != nullptr && second->kind == Kind::LobbyInvite);
    assert(second->started == start + 7s);
    assert(queue.size(start + 13s) == 1U);
    assert(Queue::alpha(*queue.current(start + 13s), start + 13s) < 1.0F);
    assert(queue.current(start + 14s) == nullptr);

    queue.push(Kind::FriendOnline, "A", "one", start + 20s);
    queue.push(Kind::FriendOnline, "B", "two", start + 20s);
    queue.push(Kind::LobbyInvite, "C", "three", start + 20s);
    assert(queue.size(start + 20s) == 2U);
    assert(queue.current(start + 20s)->title == "A");
    return 0;
}
