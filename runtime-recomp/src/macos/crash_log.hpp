#pragma once
#include <filesystem>

namespace dkr::macos {
// Set up files before entering any signal handler. No allocation or stdio in it.
void install_crash_logging(const std::filesystem::path& directory, bool enabled);
void archive_runtime_log(const std::filesystem::path& directory);
}
