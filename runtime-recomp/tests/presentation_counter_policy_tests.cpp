#include "presentation_counter_policy.hpp"

#include <cassert>
#include <cstdio>

int main() {
    using namespace dkr::runtime::presentation_counter;

    Snapshot snapshot;
    assert(consume_delta(40U, snapshot) == 0U);
    assert(consume_delta(41U, snapshot) == 1U);
    assert(consume_delta(43U, snapshot) == 2U);
    assert(consume_delta(43U, snapshot) == 0U);

    // A recreated queue resets its counter rather than underflowing into an
    // impossible FPS spike.
    assert(consume_delta(1U, snapshot) == 0U);
    assert(consume_delta(2U, snapshot) == 1U);

    // The first VI callback starts the recompiled game before RT64 has
    // necessarily completed a framebuffer. A client can enter its frame-zero
    // online wait in this interval, but there is not yet a safe image to
    // repeat.
    assert(!can_repeat_last_present(0U, true, 1U));
    assert(!can_repeat_last_present(1U, false, 1U));
    assert(!can_repeat_last_present(1U, true, 0U));
    assert(can_repeat_last_present(1U, true, 1U));

    std::puts("[test][presentation-counter] PASS");
    return 0;
}
