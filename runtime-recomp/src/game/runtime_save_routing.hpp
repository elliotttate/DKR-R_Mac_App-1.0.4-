#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace dkr::runtime::saves {

struct RuntimeOnlineSaveStatus {
    bool active = false;
    bool verified = false;
    std::uint64_t hash = 0U;
    std::filesystem::path path;
};

void reset_runtime_online_save_status();
bool activate_online_save_for_runtime(bool host, std::uint64_t match_id,
                                      std::uint64_t expected_hash,
                                      std::string& error);
RuntimeOnlineSaveStatus runtime_online_save_status();

} // namespace dkr::runtime::saves
