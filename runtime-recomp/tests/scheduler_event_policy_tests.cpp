#include "scheduler_event_policy.hpp"

#include <cassert>
#include <cstdio>

int main() {
    using dkr::runtime::scheduler::is_rdram_word_address;
    using dkr::runtime::scheduler::is_valid_dp_task_pointer;
    using dkr::runtime::scheduler::is_valid_sp_task_pointer;

    // Generic low-level accesses may legitimately use physical RDRAM offset
    // zero. Scheduler OSTask fields cannot: zero is the null task sentinel.
    assert(is_rdram_word_address(0U, 0x10U));
    assert(!is_valid_sp_task_pointer(0U));

    assert(is_valid_sp_task_pointer(0x00000004U));
    assert(is_valid_sp_task_pointer(0x80000004U));
    assert(is_valid_sp_task_pointer(0xA0000004U));
    assert(!is_valid_sp_task_pointer(0x807FFFF0U));
    assert(!is_valid_sp_task_pointer(0x80800000U));
    assert(!is_valid_sp_task_pointer(0xFFFFFFFFU));

    assert(!is_valid_dp_task_pointer(0U));
    assert(is_valid_dp_task_pointer(0x00000004U));
    assert(is_valid_dp_task_pointer(0x80000004U));
    assert(is_valid_dp_task_pointer(0xA0000004U));
    assert(is_valid_dp_task_pointer(0x807FFF94U));
    assert(!is_valid_dp_task_pointer(0x807FFF98U));
    assert(!is_valid_dp_task_pointer(0x80800000U));
    assert(!is_valid_dp_task_pointer(0xFFFFFFFFU));

    std::puts("[test][scheduler-event-policy] PASS");
    return 0;
}
