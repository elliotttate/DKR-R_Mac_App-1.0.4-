#include "online_input_policy.hpp"

#include <cassert>

int main() {
    using namespace dkr::runtime::netplay;

    static_assert(keyboard_owns_input_port(false, 2U, 2));
    static_assert(!keyboard_owns_input_port(false, 0U, 2));
    // Network slots and local control profiles are independent. Keyboard
    // ownership therefore remains attached to the explicitly selected local
    // profile in both offline and online play.
    static_assert(!keyboard_owns_input_port(true, 0U, 1));
    static_assert(keyboard_owns_input_port(true, 1U, 1));

    static_assert(online_port_occupied(true, 0b0011U, 0U));
    static_assert(online_port_occupied(true, 0b0011U, 1U));
    static_assert(!online_port_occupied(true, 0b0011U, 2U));
    static_assert(!online_port_occupied(false, 0b1111U, 0U));

    static_assert(publish_physical_input_to_virtual_port(false));
    static_assert(!publish_physical_input_to_virtual_port(true));

    assert(physical_rumble_port(false, 1U, 3U, 1) == 1);
    assert(physical_rumble_port(true, 1U, 3U, 1) == 3);
    assert(physical_rumble_port(true, 1U, 3U, 0) == -1);
}
