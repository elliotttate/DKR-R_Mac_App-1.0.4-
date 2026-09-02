#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace RT64 {
struct Application;
}

namespace dkr::runtime::texture_packs {

enum class Format {
    NativeRt64,
    RiceRt64,
    LegacyRice,
    LegacyJabo,
    Unknown,
};

struct PackInfo {
    std::string id;
    std::string name;
    std::filesystem::path path;
    std::string zip_base_path;
    Format format = Format::Unknown;
    bool compatible = false;
    bool enabled = false;
    bool hidden = false;
    std::size_t image_count = 0;
    std::uintmax_t managed_size_bytes = 0;
    std::int64_t imported_at_unix_seconds = 0;
    std::string detail;
};

struct ImportProgress {
    float fraction = 0.0F;
    std::string stage;
    bool commit_started = false;
};

// Return false from the callback to cancel before the commit begins. Once
// commit_started is true the managed destination is renamed atomically and
// cancellation is deliberately ignored so an import can never be left half
// installed.
using ImportProgressCallback = std::function<bool(const ImportProgress&)>;

void configure(const std::filesystem::path& config_directory);
void refresh();
// Monotonically increases whenever the library or its visible state changes.
// UI code can use this cheap value to avoid copying and sorting an unchanged
// texture-pack catalog every rendered frame.
std::uint64_t generation();
std::vector<PackInfo> snapshot(bool include_hidden = false);
bool import_archive(const std::filesystem::path& source, std::string& status,
                    const ImportProgressCallback& progress = {});
void set_enabled(const std::string& id, bool enabled);
bool toggle_last_selected(std::string& status);
bool set_hidden(const std::string& id, bool hidden, std::string& status);
bool delete_managed(const std::string& id, std::string& status);
void request_reload();
void apply_pending(RT64::Application& application, bool modern_profile);
std::string status();

const char* format_name(Format format);

} // namespace dkr::runtime::texture_packs
