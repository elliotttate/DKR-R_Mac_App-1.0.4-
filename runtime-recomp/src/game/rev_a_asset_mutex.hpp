#pragma once

#include <cstdint>

namespace dkr::runtime::rev_a_asset_mutex {

struct Statistics {
    std::uint64_t fast_acquires = 0;
    std::uint64_t scheduler_acquires = 0;
    std::uint64_t fast_releases = 0;
    std::uint64_t scheduler_releases = 0;
};

void reset_statistics();
[[nodiscard]] Statistics statistics();

} // namespace dkr::runtime::rev_a_asset_mutex
