#include "runtime_state.hpp"

#include <cassert>
#include <cstdint>
#include <vector>

using dkr::runtime::netplay::RuntimeState;

int main() {
    constexpr std::size_t memory_size = 4096U;
    RuntimeState state(memory_size);
    std::vector<std::uint8_t> memory(memory_size, 0x31U);
    recomp_context first{};
    recomp_context second{};
    first.r4 = 0x12345678U;
    first.f3.u64 = 0x1122334455667788ULL;
    first.mips3_float_mode = 1;
    second.r29 = 0x87654321U;
    assert(state.register_context(&first));
    assert(state.register_context(&second));
    assert(state.active_context_count() == 2U);

    std::vector<std::uint8_t> snapshot(state.snapshot_size());
    assert(state.capture(memory.data(), 91U, snapshot));

    memory.assign(memory_size, 0xFFU);
    first.r4 = 0;
    first.f3.u64 = 0;
    first.mips3_float_mode = 0;
    second.r29 = 0;
    std::uint32_t restored_frame = 0;
    assert(state.restore(memory.data(), snapshot, restored_frame));
    assert(restored_frame == 91U);
    assert(memory.front() == 0x31U && memory.back() == 0x31U);
    assert(first.r4 == 0x12345678U);
    assert(first.f3.u64 == 0x1122334455667788ULL);
    assert(first.f_odd == &first.f1.u32l);
    assert(second.r29 == 0x87654321U);

    state.unregister_context(&second);
    assert(!state.restore(memory.data(), snapshot, restored_frame));
    state.reset();
    assert(state.active_context_count() == 0U);
}
