#include "online_wait_state.hpp"

#include <cassert>

int main() {
    using dkr::runtime::netplay::OnlineWaitReason;
    using dkr::runtime::netplay::OnlineWaitState;

    OnlineWaitState state;
    assert(!state.active());
    assert(state.reason() == OnlineWaitReason::None);
    assert(state.generation() == 0U);

    state.enter(OnlineWaitReason::Transition);
    const std::uint64_t transition_generation = state.generation();
    assert(state.active());
    assert(state.reason() == OnlineWaitReason::Transition);
    assert(transition_generation != 0U);

    // Repeated servicing of the same parked authored tick must not pulse or
    // restart the notification debounce window.
    state.enter(OnlineWaitReason::Transition);
    assert(state.generation() == transition_generation);

    state.enter(OnlineWaitReason::ClientCatchUp);
    assert(state.reason() == OnlineWaitReason::ClientCatchUp);
    assert(state.generation() > transition_generation);

    state.leave();
    const std::uint64_t cleared_generation = state.generation();
    assert(!state.active());
    assert(state.reason() == OnlineWaitReason::None);

    // Clearing an already clear state is idempotent, while a later wait with
    // the same reason is still distinguishable from the completed one.
    state.leave();
    assert(state.generation() == cleared_generation);
    state.enter(OnlineWaitReason::ClientCatchUp);
    assert(state.generation() > cleared_generation);
    assert(state.active());

    state.enter(OnlineWaitReason::None);
    assert(!state.active());
    return 0;
}
