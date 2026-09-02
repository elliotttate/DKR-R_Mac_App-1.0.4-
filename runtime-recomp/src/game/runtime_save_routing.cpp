#include "runtime_save_routing.hpp"

#include "netplay/netplay_types.hpp"
#include "save_manager.hpp"

#include "ultramodern/ultramodern.hpp"

#include <mutex>
#include <string_view>
#include <vector>

namespace {

std::mutex g_runtime_save_mutex;
dkr::runtime::saves::RuntimeOnlineSaveStatus g_runtime_save_status;

std::uint64_t HashSave(const std::vector<std::uint8_t>& bytes) {
    return dkr::runtime::netplay::stable_hash(std::string_view(
        reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}

} // namespace

void dkr::runtime::saves::reset_runtime_online_save_status() {
    std::scoped_lock lock(g_runtime_save_mutex);
    g_runtime_save_status = {};
}

bool dkr::runtime::saves::activate_online_save_for_runtime(
    bool host, std::uint64_t match_id, std::uint64_t expected_hash,
    std::string& error) {
    reset_runtime_online_save_status();
    if (match_id == 0U || expected_hash == 0U) {
        error = "The synchronized online save identity is incomplete.";
        return false;
    }

    std::vector<std::uint8_t> bytes;
    std::filesystem::path expected_path;
    if (!read_online_adventure(host, match_id, bytes, expected_path, error)) {
        return false;
    }
    const std::uint64_t readback_hash = HashSave(bytes);
    if (readback_hash != expected_hash) {
        error = "The online save changed after lobby verification.";
        return false;
    }

    const std::filesystem::path subfolder =
        online_adventure_subfolder(host, match_id);
    ultramodern::change_save_file(subfolder.generic_u8string(),
                                  u8"dkr.us.v77");
    const std::filesystem::path active_path =
        ultramodern::get_save_file_path().lexically_normal();
    if (active_path != expected_path.lexically_normal()) {
        ultramodern::change_save_file(u8"", u8"dkr.us.v77");
        error = "The runtime did not activate the isolated online save path.";
        return false;
    }

    std::vector<std::uint8_t> activated_bytes;
    std::filesystem::path activated_path;
    if (!read_online_adventure(host, match_id, activated_bytes,
                               activated_path, error) ||
        activated_path.lexically_normal() != active_path ||
        HashSave(activated_bytes) != expected_hash) {
        ultramodern::change_save_file(u8"", u8"dkr.us.v77");
        error = "The active online save failed its final read-back check.";
        return false;
    }

    {
        std::scoped_lock lock(g_runtime_save_mutex);
        g_runtime_save_status.active = true;
        g_runtime_save_status.verified = true;
        g_runtime_save_status.hash = expected_hash;
        g_runtime_save_status.path = active_path;
    }
    error.clear();
    return true;
}

dkr::runtime::saves::RuntimeOnlineSaveStatus
dkr::runtime::saves::runtime_online_save_status() {
    std::scoped_lock lock(g_runtime_save_mutex);
    return g_runtime_save_status;
}
