#pragma once

#include <filesystem>
#include <cstdint>

struct SDL_Window;
typedef union SDL_Event SDL_Event;

namespace RT64 {
struct Application;
}

namespace dkr::runtime::ui {

enum class LifecycleRequest : std::uint8_t {
    None = 0,
    StopGame,
    Exit,
    Restart,
};

struct StartupResult {
    bool start_game = false;
    LifecycleRequest lifecycle_request = LifecycleRequest::None;
    std::filesystem::path rom_path;
};

void configure(const std::filesystem::path& config_directory);
void persist_settings();
// Called only after an explicitly selected renderer backend fails and RT64
// successfully recovers with Automatic. Persist the recovered choice so the
// next launch does not repeat the same failure loop.
void persist_graphics_api_fallback();
StartupResult run_startup_screen(
    SDL_Window* window,
    const std::filesystem::path& preselected_rom = {});

void attach(RT64::Application& application);
void detach(RT64::Application& application);
void draw(RT64::Application& application);
bool handle_runtime_event(SDL_Event* event);
bool input_capture_active();
void toggle_overlay();
bool overlay_visible();
LifecycleRequest lifecycle_request();
void reset_lifecycle_request();

} // namespace dkr::runtime::ui
