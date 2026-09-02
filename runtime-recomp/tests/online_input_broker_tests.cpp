#include "netplay/online_input_broker.hpp"

#include <cassert>
#include <cmath>

int main() {
    using namespace dkr::runtime::netplay;

    OnlineInputBroker broker;
    broker.configure(true, 0b0011U, 1U);
    broker.set_local_profile(2U);

    // A controller assigned to a different local profile cannot become this
    // machine's online racer by accident.
    broker.capture_local(0U, 0x8000U, 1.0F, -1.0F, false);
    assert(!broker.local_sample().valid);

    broker.capture_local(2U, 0x8000U, 0.5F, -0.5F, false);
    const LocalInputSample local = broker.local_sample();
    assert(local.valid);
    assert(local.profile == 2U);
    assert(local.input.buttons == 0x8000U);
    assert(std::abs(unpack_input_axis(local.input.stick_x) - 0.5F) < 0.01F);
    assert(std::abs(unpack_input_axis(local.input.stick_y) + 0.5F) < 0.01F);

    FrameInputs inputs{};
    inputs[0] = {0x0001U, 10, -10};
    inputs[1] = {0x0002U, 20, -20};
    inputs[2] = {0x0004U, 30, -30};
    assert(broker.publish(17U, InputFrameSource::FrontendLockstep, inputs));
    assert(broker.input_for_port(0U) == inputs[0]);
    assert(broker.input_for_port(1U) == inputs[1]);
    assert(broker.input_for_port(2U) == PackedInput{});

    // Physical polling after publication must not alter the immutable frame.
    broker.capture_local(2U, 0x4000U, -1.0F, 1.0F, false);
    assert(broker.input_for_port(0U) == inputs[0]);
    assert(broker.input_for_port(1U) == inputs[1]);

    // Re-applying identical topology is harmless; entering a new scene epoch
    // invalidates the old frame until the scheduler publishes the new one.
    broker.configure(true, 0b0011U, 1U);
    assert(broker.input_for_port(1U) == inputs[1]);
    broker.begin_epoch();
    assert(broker.input_for_port(0U) == PackedInput{});
    assert(!broker.view().committed.valid);
    // Beginning a new synchronized output epoch must not invalidate the most
    // recent private physical sample. The scheduler may need that sample to
    // seed the first input frame of the new epoch.
    assert(broker.local_sample().valid);
    assert(broker.local_sample().input.buttons == 0x4000U);

    assert(broker.publish(0U, InputFrameSource::Rollback, inputs));
    const auto committed = broker.view().committed;
    assert(committed.valid);
    assert(committed.frame == 0U);
    assert(committed.source == InputFrameSource::Rollback);

    broker.configure(false, 0U, 0xFFU);
    assert(!broker.view().enabled);
    assert(broker.input_for_port(0U) == PackedInput{});
    assert(!broker.publish(1U, InputFrameSource::GameplayLockstep, inputs));
    return 0;
}
