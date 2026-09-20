#pragma once

#include <cstdint>
#include <optional>
#include <filesystem>
#include <string>

namespace dkr::runtime::magic_codes {

void configure(const std::filesystem::path& config_directory);
void begin_game_session(std::optional<std::uint32_t> online_mask = std::nullopt);
std::uint32_t launch_mask();
std::string queue_error();

std::uint32_t persistent_mask();
void set_persistent_mask(std::uint32_t mask);

std::uint32_t queued_one_shot_mask();
bool set_queued_one_shot_mask(std::uint32_t mask, std::string& error);

std::uint32_t selected_mask();
bool set_enabled(std::uint8_t internal_index, bool enabled, std::string& error);
bool clear_all(std::string& error);

} // namespace dkr::runtime::magic_codes
