#include "runtime_hud_layout.hpp"

#include "librecomp/helpers.hpp"
#include "presentation_identity.hpp"
#include "recomp.h"
#include "revision_addresses.hpp"
#include "runtime_enhancements.hpp"
#include "runtime_platform.hpp"
#include "virtual_pak_policy.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>

namespace {

using dkr::runtime::hud::Anchor;
using dkr::runtime::hud::LayoutMode;

struct PublishedState {
    LayoutMode mode = LayoutMode::Original;
    float scale = 1.0F;
    int viewport_width = 640;
    int viewport_height = 480;
};

struct SavedElement {
    bool active = false;
    gpr address = 0;
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint32_t scale = 0;
};

std::mutex g_mutex;
std::filesystem::path g_config_directory;
LayoutMode g_mode = LayoutMode::Original;
float g_scale = 1.0F;
int g_viewport_width = 640;
int g_viewport_height = 480;
std::atomic<std::shared_ptr<const PublishedState>> g_published;
thread_local SavedElement g_saved_element;
thread_local SavedElement g_saved_minimap;
thread_local bool g_player_pass_active = false;
thread_local bool g_general_pass_active = false;
thread_local bool g_dialogue_pass_active = false;
thread_local std::uint32_t g_dialogue_pass_holder = 0U;

constexpr std::uint8_t kHudPassMarkerBeginMode = 1U;
constexpr std::uint8_t kHudPassMarkerEndMode = 0U;
constexpr std::uint8_t kHudPassMarkerVariant = 29U;

gpr RdramAddress(std::uint32_t address) {
    return static_cast<gpr>(static_cast<std::int32_t>(address));
}

float ReadFloat(std::uint8_t* rdram, gpr address, int offset) {
    return std::bit_cast<float>(static_cast<std::uint32_t>(MEM_W(offset, address)));
}

void WriteFloat(std::uint8_t* rdram, gpr address, int offset, float value) {
    MEM_W(offset, address) = std::bit_cast<std::uint32_t>(value);
}

bool IsRdramPointer(std::uint32_t value, std::uint32_t size) {
    if (value < 0x80000000U || value > 0x807FFFFFU) return false;
    const std::uint32_t offset = value & 0x00FFFFFFU;
    return offset <= 0x00800000U && size <= 0x00800000U - offset;
}

std::filesystem::path SettingsPath() {
    return g_config_directory / "hud-placement.ini";
}

std::filesystem::path BackupPath() {
    return g_config_directory / "hud-placement.ini.bak";
}

std::filesystem::path LegacyPath() {
    return g_config_directory / "hud-layouts-v1.ini";
}

void PublishLocked() {
    auto state = std::make_shared<PublishedState>();
    state->mode = g_mode;
    state->scale = g_scale;
    state->viewport_width = g_viewport_width;
    state->viewport_height = g_viewport_height;
    g_published.store(std::move(state), std::memory_order_release);
}

bool LoadFile(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) return false;

    int mode = static_cast<int>(LayoutMode::Original);
    float scale = 1.0F;
    std::string line;
    while (std::getline(input, line)) {
        const std::size_t equals = line.find('=');
        if (equals == std::string::npos) continue;
        const std::string key = line.substr(0, equals);
        const std::string value = line.substr(equals + 1U);
        try {
            if (key == "mode") mode = std::stoi(value);
            else if (key == "scale") scale = std::stof(value);
        } catch (...) {
            // Keep the last valid value when a user-edited line is malformed.
        }
    }
    g_mode = static_cast<LayoutMode>(std::clamp(
        mode, static_cast<int>(LayoutMode::Original),
        static_cast<int>(LayoutMode::FitToViewport)));
    g_scale = dkr::runtime::hud::clamp_hud_scale(scale);
    return true;
}

void SaveLocked() {
    if (g_config_directory.empty()) return;
    std::error_code error;
    std::filesystem::create_directories(g_config_directory, error);
    const auto target = SettingsPath();
    const auto temporary = target.string() + ".tmp";
    const auto backup = BackupPath();
    {
        std::ofstream output(temporary, std::ios::trunc);
        if (!output) return;
        output << "version=1\nmode=" << static_cast<int>(g_mode)
               << "\nscale=" << g_scale << '\n';
        output.flush();
        if (!output) return;
    }
    std::filesystem::remove(backup, error);
    error.clear();
    if (std::filesystem::exists(target, error)) {
        error.clear();
        std::filesystem::rename(target, backup, error);
        if (error) {
            std::filesystem::remove(temporary, error);
            return;
        }
    }
    error.clear();
    std::filesystem::rename(temporary, target, error);
    if (error && std::filesystem::exists(backup)) {
        std::error_code restore_error;
        std::filesystem::rename(backup, target, restore_error);
    }
}

void LoadLocked() {
    g_mode = LayoutMode::Original;
    g_scale = 1.0F;
    if (LoadFile(SettingsPath())) return;
    if (LoadFile(BackupPath())) {
        SaveLocked();
        return;
    }
    // Preserve the useful mode/scale portion of Workshop-era settings once,
    // while deliberately ignoring custom profiles that are no longer exposed.
    if (LoadFile(LegacyPath())) SaveLocked();
}

std::size_t ResolveElementIndex(std::uint8_t* rdram, std::uint32_t element) {
    constexpr std::uint32_t kElementSize = 0x20U;
    constexpr std::uint32_t kHudDataSize =
        static_cast<std::uint32_t>(dkr::runtime::hud::kElements.size()) *
        kElementSize;
    for (int player = 0; player < 4; ++player) {
        const std::uint32_t base = static_cast<std::uint32_t>(MEM_W(
            player * 4,
            RdramAddress(dkr::runtime::revision_addresses::PlayerHud)));
        if (IsRdramPointer(base, kHudDataSize) && element >= base &&
            element < base + kHudDataSize &&
            ((element - base) % kElementSize) == 0U) {
            return (element - base) / kElementSize;
        }
    }
    const std::uint32_t base = static_cast<std::uint32_t>(MEM_W(
        0, RdramAddress(dkr::runtime::revision_addresses::CurrentHud)));
    if (IsRdramPointer(base, kHudDataSize) && element >= base &&
        element < base + kHudDataSize &&
        ((element - base) % kElementSize) == 0U) {
        return (element - base) / kElementSize;
    }
    return dkr::runtime::hud::kElements.size();
}

float VerticalDelta(LayoutMode mode, Anchor anchor) {
    if (mode != LayoutMode::SafeArea) return 0.0F;
    if (anchor == Anchor::TopLeft || anchor == Anchor::TopCentre ||
        anchor == Anchor::TopRight) return -4.0F;
    if (anchor == Anchor::BottomLeft || anchor == Anchor::BottomCentre ||
        anchor == Anchor::BottomRight) return 4.0F;
    return 0.0F;
}

void ApplyPlacement(std::uint8_t* rdram, gpr address, Anchor anchor,
                    const PublishedState& state,
                    dkr::runtime::hud::ViewportClass viewport_class) {
    float x = ReadFloat(rdram, address, 0x0C);
    float y = ReadFloat(rdram, address, 0x10);
    float scale = ReadFloat(rdram, address, 0x08);
    const float host_aspect = state.viewport_height > 0
        ? static_cast<float>(state.viewport_width) /
              static_cast<float>(state.viewport_height)
        : 4.0F / 3.0F;
    // Translate in DKR's authored coordinate space. Every draw belonging to a
    // composite widget receives the exact same delta, preserving the original
    // icon/digit/dial spacing and aspect ratio. The RT64 bridge only widens
    // clipping for the complete HUD pass; it never changes an element's
    // viewport or rectangle origin.
    x += dkr::runtime::hud::placement_delta_x(
        state.mode, anchor, host_aspect, viewport_class);
    y += VerticalDelta(state.mode, anchor);
    scale *= state.scale;
    WriteFloat(rdram, address, 0x0C, x);
    WriteFloat(rdram, address, 0x10, y);
    WriteFloat(rdram, address, 0x08, scale);
}

bool RecordHudMarker(std::uint8_t* rdram, std::uint32_t holder,
                     std::uint8_t mode, std::uint16_t token,
                     std::uint8_t variant) {
    if (rdram == nullptr || !IsRdramPointer(holder, sizeof(std::uint32_t))) {
        return false;
    }
    const std::uint32_t command = static_cast<std::uint32_t>(MEM_W(
        0, static_cast<gpr>(static_cast<std::int32_t>(holder))));
    if (!IsRdramPointer(command, 8U)) return false;
    return dkr::runtime::presentation::record_presentation_marker(
        command, mode, token, variant);
}

bool RecordHudPassMarker(std::uint8_t* rdram, std::uint32_t holder,
                         bool begin, std::uint16_t viewport_cover_token) {
    return RecordHudMarker(
        rdram, holder,
        begin ? kHudPassMarkerBeginMode : kHudPassMarkerEndMode,
        begin ? viewport_cover_token : 0U, kHudPassMarkerVariant);
}

bool BeginHudPassForHolder(std::uint8_t* rdram, std::uint32_t holder) {
    if (rdram == nullptr ||
        !dkr::runtime::enhancements::modern_presentation_enabled()) {
        return false;
    }
    const auto state = g_published.load(std::memory_order_acquire);
    if (!state || state->mode != LayoutMode::FitToViewport) return false;
    const float host_aspect = state->viewport_height > 0
        ? static_cast<float>(state->viewport_width) /
              static_cast<float>(state->viewport_height)
        : 4.0F / 3.0F;
    return RecordHudPassMarker(
        rdram, holder, true,
        dkr::runtime::hud::encode_hud_viewport_cover(host_aspect));
}

bool BeginHudPass(std::uint8_t* rdram, recomp_context* context,
                  int holder_stack_offset) {
    if (rdram == nullptr || context == nullptr) return false;
    return BeginHudPassForHolder(
        rdram,
        static_cast<std::uint32_t>(MEM_W(holder_stack_offset, context->r29)));
}

void EndHudPassForHolder(std::uint8_t* rdram, std::uint32_t holder,
                         bool& active) {
    if (!active || rdram == nullptr) return;
    RecordHudPassMarker(rdram, holder, false, 0U);
    active = false;
}

void EndHudPass(std::uint8_t* rdram, recomp_context* context,
                int holder_stack_offset, bool& active) {
    if (!active || rdram == nullptr || context == nullptr) return;
    EndHudPassForHolder(
        rdram,
        static_cast<std::uint32_t>(MEM_W(holder_stack_offset, context->r29)),
        active);
}

} // namespace

void dkr::runtime::hud::configure(
    const std::filesystem::path& config_directory) {
    std::scoped_lock lock(g_mutex);
    g_config_directory = config_directory;
    LoadLocked();
    PublishLocked();
}

LayoutMode dkr::runtime::hud::mode() {
    std::scoped_lock lock(g_mutex);
    return g_mode;
}

void dkr::runtime::hud::set_mode(LayoutMode value) {
    std::scoped_lock lock(g_mutex);
    g_mode = static_cast<LayoutMode>(std::clamp(
        static_cast<int>(value), static_cast<int>(LayoutMode::Original),
        static_cast<int>(LayoutMode::FitToViewport)));
    PublishLocked();
    SaveLocked();
}

float dkr::runtime::hud::global_scale() {
    std::scoped_lock lock(g_mutex);
    return g_scale;
}

void dkr::runtime::hud::set_global_scale(float value) {
    std::scoped_lock lock(g_mutex);
    g_scale = clamp_hud_scale(value);
    PublishLocked();
    SaveLocked();
}

void dkr::runtime::hud::set_viewport_extent(int width, int height) {
    if (width <= 0 || height <= 0) return;
    std::scoped_lock lock(g_mutex);
    if (width == g_viewport_width && height == g_viewport_height) return;
    g_viewport_width = width;
    g_viewport_height = height;
    PublishLocked();
}

void dkr::runtime::hud::begin_element(std::uint8_t* rdram,
                                      recomp_context* context) {
    g_saved_element = {};
    if (rdram == nullptr || context == nullptr ||
        !dkr::runtime::enhancements::modern_presentation_enabled()) return;
    const auto state = g_published.load(std::memory_order_acquire);
    if (!state || (state->mode == LayoutMode::Original &&
                   state->scale == 1.0F)) return;
    const std::uint32_t raw = static_cast<std::uint32_t>(context->r7);
    if (!IsRdramPointer(raw, 0x20U)) return;
    const gpr address = static_cast<gpr>(static_cast<std::int32_t>(raw));
    const std::size_t index = ResolveElementIndex(rdram, raw);
    const float authored_x = ReadFloat(rdram, address, 0x0C);
    const Anchor anchor = dkr::runtime::hud::effective_anchor(
        index, authored_x);
    g_saved_element = {
        true, address,
        static_cast<std::uint32_t>(MEM_W(0x0C, address)),
        static_cast<std::uint32_t>(MEM_W(0x10, address)),
        static_cast<std::uint32_t>(MEM_W(0x08, address))};
    const int layout = static_cast<int>(MEM_W(
        0, RdramAddress(dkr::runtime::revision_addresses::ViewportLayout)));
    ApplyPlacement(rdram, address, anchor, *state,
                   dkr::runtime::hud::viewport_class_for_layout(layout));
}

void dkr::runtime::hud::begin_minimap(std::uint8_t* rdram,
                                      recomp_context* context) {
    g_saved_minimap = {};
    if (rdram == nullptr || context == nullptr ||
        !dkr::runtime::enhancements::modern_presentation_enabled()) return;
    const auto state = g_published.load(std::memory_order_acquire);
    if (!state || (state->mode == LayoutMode::Original &&
                   state->scale == 1.0F)) return;
    const gpr address = static_cast<gpr>(context->r29 + 0x120U);
    const std::uint32_t raw = static_cast<std::uint32_t>(address);
    if (!IsRdramPointer(raw, 0x20U)) return;
    g_saved_minimap = {
        true, address,
        static_cast<std::uint32_t>(MEM_W(0x0C, address)),
        static_cast<std::uint32_t>(MEM_W(0x10, address)),
        static_cast<std::uint32_t>(MEM_W(0x08, address))};
    const Anchor anchor = kSupplementalElements[0].anchor;
    const int layout = static_cast<int>(MEM_W(
        0, RdramAddress(dkr::runtime::revision_addresses::ViewportLayout)));
    ApplyPlacement(rdram, address, anchor, *state,
                   dkr::runtime::hud::viewport_class_for_layout(layout));
}

void dkr::runtime::hud::end_element(std::uint8_t* rdram,
                                    recomp_context* context) {
    if (!g_saved_element.active || rdram == nullptr) return;
    MEM_W(0x0C, g_saved_element.address) = g_saved_element.x;
    MEM_W(0x10, g_saved_element.address) = g_saved_element.y;
    MEM_W(0x08, g_saved_element.address) = g_saved_element.scale;
    g_saved_element = {};
}

void dkr::runtime::hud::end_minimap(std::uint8_t* rdram,
                                    recomp_context*) {
    if (!g_saved_minimap.active || rdram == nullptr) return;
    MEM_W(0x0C, g_saved_minimap.address) = g_saved_minimap.x;
    MEM_W(0x10, g_saved_minimap.address) = g_saved_minimap.y;
    MEM_W(0x08, g_saved_minimap.address) = g_saved_minimap.scale;
    g_saved_minimap = {};
}

void dkr::runtime::hud::begin_player_pass(std::uint8_t* rdram,
                                           recomp_context* context) {
    g_player_pass_active = BeginHudPass(rdram, context, 0x30);
}

void dkr::runtime::hud::end_player_pass(std::uint8_t* rdram,
                                         recomp_context* context) {
    EndHudPass(rdram, context, 0x30, g_player_pass_active);
}

void dkr::runtime::hud::begin_general_pass(std::uint8_t* rdram,
                                            recomp_context* context) {
    g_general_pass_active = BeginHudPass(rdram, context, 0x160);
}

void dkr::runtime::hud::end_general_pass(std::uint8_t* rdram,
                                          recomp_context* context) {
    EndHudPass(rdram, context, 0x160, g_general_pass_active);
}

void dkr::runtime::hud::begin_dialogue_pass(std::uint8_t* rdram,
                                             recomp_context* context) {
    g_dialogue_pass_active = false;
    g_dialogue_pass_holder = 0U;
    if (context == nullptr) return;

    // render_dialogue_boxes receives Gfx ** as its first argument. Keep the
    // original holder because the function is free to reuse argument
    // registers before its common return hook runs.
    g_dialogue_pass_holder = static_cast<std::uint32_t>(context->r4);
    g_dialogue_pass_active = BeginHudPassForHolder(
        rdram, g_dialogue_pass_holder);
}

void dkr::runtime::hud::end_dialogue_pass(std::uint8_t* rdram,
                                           recomp_context*) {
    EndHudPassForHolder(
        rdram, g_dialogue_pass_holder, g_dialogue_pass_active);
    g_dialogue_pass_holder = 0U;
}

extern "C" void dkr_hud_element_begin(std::uint8_t* rdram,
                                        recomp_context* context) {
    dkr::runtime::hud::begin_element(rdram, context);
}

extern "C" void dkr_hud_element_end(std::uint8_t* rdram,
                                      recomp_context* context) {
    dkr::runtime::hud::end_element(rdram, context);
}

extern "C" void dkr_hud_minimap_begin(std::uint8_t* rdram,
                                        recomp_context* context) {
    dkr::runtime::hud::begin_minimap(rdram, context);
}

extern "C" void dkr_hud_minimap_end(std::uint8_t* rdram,
                                      recomp_context* context) {
    dkr::runtime::hud::end_minimap(rdram, context);
}

extern "C" void dkr_hud_player_pass_begin(std::uint8_t* rdram,
                                            recomp_context* context) {
    dkr::runtime::hud::begin_player_pass(rdram, context);
}

extern "C" void dkr_hud_player_pass_end(std::uint8_t* rdram,
                                          recomp_context* context) {
    dkr::runtime::hud::end_player_pass(rdram, context);
}

extern "C" void dkr_hud_general_pass_begin(std::uint8_t* rdram,
                                             recomp_context* context) {
    dkr::runtime::hud::begin_general_pass(rdram, context);
}

extern "C" void dkr_hud_general_pass_end(std::uint8_t* rdram,
                                           recomp_context* context) {
    dkr::runtime::hud::end_general_pass(rdram, context);
}

extern "C" void dkr_hud_dialogue_pass_begin(std::uint8_t* rdram,
                                              recomp_context* context) {
    dkr::runtime::hud::begin_dialogue_pass(rdram, context);
}

extern "C" void dkr_hud_dialogue_pass_end(std::uint8_t* rdram,
                                            recomp_context* context) {
    dkr::runtime::hud::end_dialogue_pass(rdram, context);
}

extern "C" void dkr_refresh_combined_accessories(std::uint8_t* rdram,
                                                    recomp_context*) {
    if (rdram == nullptr) return;
    std::array<bool, 4> assigned{};
    std::array<bool, 4> connected{};
    std::array<bool, 4> rumble_capable{};
    for (std::size_t port = 0; port < 4; ++port) {
        const auto status =
            dkr::runtime::platform::player_controller_status(port);
        assigned[port] = status.assigned;
        connected[port] = status.connected;
        rumble_capable[port] = status.rumble;
    }
    const std::uint8_t retail = MEM_B(
        0, RdramAddress(dkr::runtime::revision_addresses::RumblePresent));
    MEM_B(0, RdramAddress(dkr::runtime::revision_addresses::RumblePresent)) =
        dkr::runtime::pak::policy::combined_rumble_mask(
            dkr::runtime::platform::rumble_enabled(), retail,
            assigned, connected, rumble_capable);
}
