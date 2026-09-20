#pragma once

#include <filesystem>
#include <string>

namespace dkr::runtime::pak {

void configure(const std::filesystem::path& config_directory);
// Only with all guest threads stopped. Empty restores ordinary Paks; cached
// files from the previous save namespace are discarded, never copied across.
void begin_session_directory(const std::filesystem::path& directory);
bool enabled();
void set_enabled(bool enabled);
bool self_test(const std::filesystem::path& directory, std::string& error);

} // namespace dkr::runtime::pak
