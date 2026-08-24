#include "runtime_platform.hpp"
#include "audio_equalizer.hpp"
#include "runtime_input.hpp"
#include "runtime_enhancements.hpp"
#include "runtime_telemetry.hpp"
#include "ultramodern/ultramodern.hpp"

#if DKR_RUNTIME_HAS_RT64
#include "runtime_ui.hpp"
#include "imgui/imgui.h"
#include <SDL.h>
#if defined(_WIN32) || defined(__APPLE__)
#include <SDL_syswm.h>
#endif
#if defined(__APPLE__)
extern "C" void* dkr_create_metal_layer(void* ns_window_ptr);
extern "C" void dkr_destroy_metal_layer(void* layer_ptr);
#endif
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <mutex>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

namespace {

std::atomic<std::uint64_t> g_audio_buffers{0};
constexpr std::size_t kControllerCount = 4;
std::array<std::atomic<std::uint16_t>, kControllerCount> g_buttons{};
std::array<std::atomic<float>, kControllerCount> g_stick_x{};
std::array<std::atomic<float>, kControllerCount> g_stick_y{};
std::atomic<float> g_master_volume{1.0F};
std::atomic<float> g_bass_gain{0.0F};
std::atomic<float> g_mid_gain{0.0F};
std::atomic<float> g_treble_gain{0.0F};
std::atomic<bool> g_rumble_enabled{true};
std::atomic<float> g_rumble_strength{1.0F};

#if DKR_RUNTIME_HAS_RT64
std::mutex g_platform_mutex;
SDL_AudioDeviceID g_audio_device = 0;
std::array<SDL_GameController*, kControllerCount> g_controllers{};
SDL_GameController* g_gyro_controller = nullptr;
SDL_Window* g_window = nullptr;
#if defined(__APPLE__)
void* g_metal_layer = nullptr;
#endif
std::uint32_t g_audio_frequency = 0;
std::vector<std::int16_t> g_audio_swap_buffer;
dkr::runtime::audio::StereoEqualizer g_audio_equalizer;
std::uint32_t g_audio_callback_frames = 0;
bool g_audio_playback_started = false;
std::size_t g_audio_nominal_block_frames = 0;
std::size_t g_audio_prime_target_frames = 0;
std::uint32_t g_audio_cushion_blocks = 2;
constexpr std::uint64_t kFullscreenCursorIdleMs = 3'000U;
std::uint64_t g_cursor_last_activity_ms = 0;
bool g_cursor_hidden = false;
bool g_cursor_was_fullscreen = false;

float NormaliseAxis(Sint16 value, Sint16 deadzone = 7849) {
    const int magnitude = std::abs(static_cast<int>(value));
    if (magnitude <= deadzone) {
        return 0.0F;
    }
    const float scaled = static_cast<float>(magnitude - deadzone) /
                         static_cast<float>(32767 - deadzone);
    return std::copysign(std::min(scaled, 1.0F), static_cast<float>(value));
}

void RefreshControllers() {
    for (SDL_GameController*& controller : g_controllers) {
        if (controller != nullptr &&
            SDL_GameControllerGetAttached(controller) != SDL_TRUE) {
            if (controller == g_gyro_controller) {
                g_gyro_controller = nullptr;
            }
            SDL_GameControllerClose(controller);
            controller = nullptr;
        }
    }
    for (int index = 0; index < SDL_NumJoysticks(); ++index) {
        if (SDL_IsGameController(index) != SDL_TRUE) {
            continue;
        }
        const SDL_JoystickID instance = SDL_JoystickGetDeviceInstanceID(index);
        const bool already_open = std::any_of(
            g_controllers.begin(), g_controllers.end(), [instance](SDL_GameController* controller) {
                return controller != nullptr && SDL_JoystickInstanceID(
                    SDL_GameControllerGetJoystick(controller)) == instance;
            });
        if (already_open) {
            continue;
        }
        const auto empty = std::find(g_controllers.begin(), g_controllers.end(), nullptr);
        if (empty == g_controllers.end()) {
            break;
        }
        *empty = SDL_GameControllerOpen(index);
        if (*empty != nullptr) {
            const auto player = static_cast<std::size_t>(empty - g_controllers.begin());
            std::fprintf(stderr, "[boot][input] player=%zu controller=%s\n", player + 1U,
                         SDL_GameControllerName(*empty));
        }
    }

    // Steam Input commonly exposes the virtual gameplay pad first and the
    // Steam Deck's physical HID controller (which owns the gyro) second. Keep
    // those roles independent: controls remain on Player 1 while motion can
    // come from any attached controller that actually exposes SDL_SENSOR_GYRO.
    SDL_GameController* selected_gyro = nullptr;
    if (g_controllers[0] != nullptr &&
        SDL_GameControllerHasSensor(g_controllers[0], SDL_SENSOR_GYRO) == SDL_TRUE) {
        selected_gyro = g_controllers[0];
    } else {
        const auto sensor = std::find_if(
            g_controllers.begin(), g_controllers.end(),
            [](SDL_GameController* controller) {
                return controller != nullptr &&
                    SDL_GameControllerHasSensor(
                        controller, SDL_SENSOR_GYRO) == SDL_TRUE;
            });
        if (sensor != g_controllers.end()) {
            selected_gyro = *sensor;
        }
    }
    if (selected_gyro != g_gyro_controller) {
        g_gyro_controller = selected_gyro;
        if (g_gyro_controller != nullptr) {
            if (SDL_GameControllerSetSensorEnabled(
                    g_gyro_controller, SDL_SENSOR_GYRO, SDL_TRUE) == 0) {
                std::fprintf(stderr,
                             "[boot][input] gyro source=%s sensor enabled\n",
                             SDL_GameControllerName(g_gyro_controller));
            } else {
                std::fprintf(stderr,
                             "[boot][input] gyro source=%s enable failed: %s\n",
                             SDL_GameControllerName(g_gyro_controller),
                             SDL_GetError());
            }
        } else {
            std::fprintf(stderr, "[boot][input] no controller gyro exposed\n");
        }
    }
}
#endif

} // namespace

bool dkr::runtime::platform::initialise() {
#if DKR_RUNTIME_HAS_RT64
    // Request SDL's direct HID paths before controller discovery. String
    // literals retain source compatibility with the bundled Windows SDL while
    // allowing newer Linux/SteamOS SDL builds to expose the built-in Deck pad.
    SDL_SetHint("SDL_JOYSTICK_HIDAPI", "1");
    SDL_SetHint("SDL_JOYSTICK_HIDAPI_STEAMDECK", "1");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER |
                 SDL_INIT_HAPTIC | SDL_INIT_SENSOR) != 0) {
        std::fprintf(stderr, "[boot][platform] SDL initialization failed: %s\n", SDL_GetError());
        return false;
    }
    SDL_version linked_version{};
    SDL_GetVersion(&linked_version);
    std::fprintf(stderr, "[boot][platform] SDL linked=%u.%u.%u\n",
                 linked_version.major, linked_version.minor,
                 linked_version.patch);
    SDL_GameControllerEventState(SDL_ENABLE);
    std::scoped_lock lock(g_platform_mutex);
    RefreshControllers();
#endif
    std::fprintf(stderr,
                 "[boot][input] keyboard: WASD=stick arrows=d-pad Space=A Shift=B "
                 "Z=Z Enter=Start IJKL=C Q=L E=R\n");
    return true;
}

void dkr::runtime::platform::shutdown() {
#if DKR_RUNTIME_HAS_RT64
    std::scoped_lock lock(g_platform_mutex);
    if (g_cursor_hidden) {
        SDL_ShowCursor(SDL_ENABLE);
        g_cursor_hidden = false;
    }
    if (g_audio_device != 0) {
        SDL_ClearQueuedAudio(g_audio_device);
        SDL_CloseAudioDevice(g_audio_device);
        g_audio_device = 0;
    }
    for (SDL_GameController*& controller : g_controllers) {
        if (controller != nullptr) {
            SDL_GameControllerClose(controller);
            controller = nullptr;
        }
    }
    g_gyro_controller = nullptr;
#if defined(__APPLE__)
    dkr_destroy_metal_layer(g_metal_layer);
    g_metal_layer = nullptr;
#endif
    if (g_window != nullptr) {
        SDL_DestroyWindow(g_window);
        g_window = nullptr;
    }
    SDL_Quit();
#endif
}

#if DKR_RUNTIME_HAS_RT64
ultramodern::renderer::WindowHandle dkr::runtime::platform::create_window() {
    if (g_window == nullptr) {
        Uint32 flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
#if defined(__linux__)
        flags |= SDL_WINDOW_VULKAN;
#endif
        g_window = SDL_CreateWindow("DKR-R - Diddy Kong Racing Recompiled", SDL_WINDOWPOS_CENTERED,
                                    SDL_WINDOWPOS_CENTERED,
                                    1440, 900, flags);
        if (g_window == nullptr) {
            std::fprintf(stderr, "[boot][window] SDL creation failed: %s\n", SDL_GetError());
        } else {
            // Both the launcher and the transparent in-game overlay are fully
            // responsive down to this size. Prevent a window-manager resize
            // from making controller targets or binding cards unusably small.
            SDL_SetWindowMinimumSize(g_window, 800, 600);
        }
    }

#if defined(_WIN32)
    SDL_SysWMinfo info{};
    SDL_VERSION(&info.version);
    if (g_window == nullptr || SDL_GetWindowWMInfo(g_window, &info) != SDL_TRUE) {
        std::fprintf(stderr, "[boot][window] native handle failed: %s\n", SDL_GetError());
        return {};
    }
    return {.window = info.info.win.window, .thread_id = GetCurrentThreadId()};
#elif defined(__APPLE__)
    SDL_SysWMinfo info{};
    SDL_VERSION(&info.version);
    if (g_window == nullptr || SDL_GetWindowWMInfo(g_window, &info) != SDL_TRUE) {
        std::fprintf(stderr, "[boot][window] native handle failed: %s\n", SDL_GetError());
        return {};
    }
    if (g_metal_layer == nullptr) {
        g_metal_layer = dkr_create_metal_layer(info.info.cocoa.window);
    }
    return {.window = info.info.cocoa.window, .view = g_metal_layer};
#else
    return g_window;
#endif
}

ultramodern::renderer::WindowHandle dkr::runtime::platform::prepare_window_for_game() {
#if defined(__linux__)
    if (g_window != nullptr) {
        const Uint32 existing_flags = SDL_GetWindowFlags(g_window);
        std::fprintf(stderr,
                     "[boot][window] launcher handoff flags=0x%08X vulkan=%s\n",
                     static_cast<unsigned>(existing_flags),
                     (existing_flags & SDL_WINDOW_VULKAN) != 0 ? "yes" : "no");
        if ((existing_flags & SDL_WINDOW_VULKAN) == 0) {
            int x = SDL_WINDOWPOS_CENTERED;
            int y = SDL_WINDOWPOS_CENTERED;
            int width = 1440;
            int height = 900;
            SDL_GetWindowPosition(g_window, &x, &y);
            SDL_GetWindowSize(g_window, &width, &height);
            const bool was_hidden = (existing_flags & SDL_WINDOW_HIDDEN) != 0;
            SDL_DestroyWindow(g_window);
            g_window = nullptr;

            Uint32 replacement_flags =
                SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_VULKAN;
            if (was_hidden) {
                replacement_flags |= SDL_WINDOW_HIDDEN;
            }
            g_window = SDL_CreateWindow("DKR-R - Diddy Kong Racing Recompiled", x, y, width, height,
                                        replacement_flags);
            if (g_window == nullptr) {
                std::fprintf(stderr,
                             "[boot][window] Vulkan handoff recreation failed: %s\n",
                             SDL_GetError());
                return {};
            }
            SDL_SetWindowMinimumSize(g_window, 800, 600);
            std::fprintf(stderr,
                         "[boot][window] recreated Vulkan-capable game window "
                         "flags=0x%08X\n",
                         static_cast<unsigned>(SDL_GetWindowFlags(g_window)));
        }
    }
#endif
    return create_window();
}

void dkr::runtime::platform::pump_window_events(void*) {
    if (dkr::runtime::ui::lifecycle_request() !=
        dkr::runtime::ui::LifecycleRequest::None) {
        ultramodern::quit();
        return;
    }
    SDL_Event event{};
    while (SDL_PollEvent(&event) != 0) {
        update_fullscreen_cursor(&event);
        if (event.type == SDL_QUIT ||
            (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE)) {
            ultramodern::quit();
            return;
        }

        // Overlay lifecycle commands are host-global. Process them before
        // forwarding the event to ImGui so a focused widget cannot swallow
        // Escape, F1 or Back/View and leave the player trapped in the UI.
        const bool keyboard_toggle = event.type == SDL_KEYDOWN && event.key.repeat == 0 &&
            (event.key.keysym.scancode == SDL_SCANCODE_ESCAPE ||
             event.key.keysym.scancode == SDL_SCANCODE_F1);
        const bool controller_toggle = event.type == SDL_CONTROLLERBUTTONDOWN &&
            event.cbutton.button == SDL_CONTROLLER_BUTTON_BACK;
        if (dkr::runtime::ui::input_capture_active()) {
            dkr::runtime::ui::handle_runtime_event(&event);
            continue;
        }
        if (handle_window_shortcut(&event, true)) {
            continue;
        }
        if (keyboard_toggle || controller_toggle) {
            dkr::runtime::ui::toggle_overlay();
            continue;
        }

        dkr::runtime::ui::handle_runtime_event(&event);
    }
    if (dkr::runtime::ui::lifecycle_request() !=
        dkr::runtime::ui::LifecycleRequest::None) {
        ultramodern::quit();
    }
    update_fullscreen_cursor();
}

void* dkr::runtime::platform::sdl_window() {
    return g_window;
}

bool dkr::runtime::platform::handle_window_shortcut(
    const void* raw_event, bool renderer_active) {
    const auto* event = static_cast<const SDL_Event*>(raw_event);
    if (event == nullptr || event->type != SDL_KEYDOWN || event->key.repeat != 0) {
        return false;
    }

    const bool f11 = event->key.keysym.scancode == SDL_SCANCODE_F11;
    const bool alt_enter = event->key.keysym.scancode == SDL_SCANCODE_RETURN &&
        (event->key.keysym.mod & KMOD_ALT) != 0;
    if (!f11 && !alt_enter) {
        return false;
    }

    auto config = ultramodern::renderer::get_graphics_config();
    const bool fullscreen =
        config.wm_option == ultramodern::renderer::WindowMode::Fullscreen;
    config.wm_option = fullscreen
        ? ultramodern::renderer::WindowMode::Windowed
        : ultramodern::renderer::WindowMode::Fullscreen;
    ultramodern::renderer::set_graphics_config(config);
    dkr::runtime::ui::persist_settings();

    // The launcher has no RT64 swap chain yet, so SDL owns this transition.
    // In game, publishing GraphicsConfig lets RT64 switch at its safe update
    // boundary instead of changing the native window underneath a present.
    if (!renderer_active && g_window != nullptr) {
        const Uint32 mode = fullscreen ? 0U : SDL_WINDOW_FULLSCREEN_DESKTOP;
        if (SDL_SetWindowFullscreen(g_window, mode) != 0) {
            std::fprintf(stderr, "[window] fullscreen toggle failed: %s\n",
                         SDL_GetError());
        }
    }

    g_cursor_last_activity_ms = SDL_GetTicks64();
    if (g_cursor_hidden) {
        SDL_ShowCursor(SDL_ENABLE);
        g_cursor_hidden = false;
    }
    return true;
}

void dkr::runtime::platform::update_fullscreen_cursor(const void* raw_event) {
    const auto* event = static_cast<const SDL_Event*>(raw_event);
    const std::uint64_t now = SDL_GetTicks64();
    if (g_cursor_last_activity_ms == 0U) {
        g_cursor_last_activity_ms = now;
    }

    bool pointer_activity = false;
    if (event != nullptr) {
        pointer_activity = event->type == SDL_MOUSEMOTION ||
            event->type == SDL_MOUSEBUTTONDOWN ||
            event->type == SDL_MOUSEBUTTONUP || event->type == SDL_MOUSEWHEEL;
        if (event->type == SDL_WINDOWEVENT &&
            (event->window.event == SDL_WINDOWEVENT_FOCUS_GAINED ||
             event->window.event == SDL_WINDOWEVENT_ENTER)) {
            pointer_activity = true;
        }
    }

    const auto& config = ultramodern::renderer::get_graphics_config();
    const Uint32 flags = g_window != nullptr ? SDL_GetWindowFlags(g_window) : 0U;
    const bool fullscreen =
        config.wm_option == ultramodern::renderer::WindowMode::Fullscreen ||
        (flags & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP)) != 0U;
    const bool focused = g_window != nullptr &&
        (flags & SDL_WINDOW_INPUT_FOCUS) != 0U;

    if (pointer_activity || fullscreen != g_cursor_was_fullscreen) {
        g_cursor_last_activity_ms = now;
        if (g_cursor_hidden) {
            SDL_ShowCursor(SDL_ENABLE);
            g_cursor_hidden = false;
        }
    }
    g_cursor_was_fullscreen = fullscreen;

    const bool should_hide = fullscreen && focused &&
        now - g_cursor_last_activity_ms >= kFullscreenCursorIdleMs;
    if (should_hide != g_cursor_hidden) {
        SDL_ShowCursor(should_hide ? SDL_DISABLE : SDL_ENABLE);
        g_cursor_hidden = should_hide;
    }
}

void dkr::runtime::platform::update_ui_gamepad_navigation() {
    ImGuiIO& io = ImGui::GetIO();
    std::scoped_lock lock(g_platform_mutex);
    SDL_GameControllerUpdate();
    RefreshControllers();

    SDL_GameController* controller = g_controllers[0];
    const bool connected = controller != nullptr;
    if (connected) {
        io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    } else {
        io.BackendFlags &= ~ImGuiBackendFlags_HasGamepad;
    }

    const auto button = [&](ImGuiKey key, SDL_GameControllerButton source) {
        io.AddKeyEvent(key, connected &&
            SDL_GameControllerGetButton(controller, source) != 0);
    };
    const auto axis = [&](ImGuiKey negative_key, ImGuiKey positive_key,
                          SDL_GameControllerAxis source, bool invert = false) {
        float value = connected
            ? NormaliseAxis(SDL_GameControllerGetAxis(controller, source))
            : 0.0F;
        if (invert) {
            value = -value;
        }
        io.AddKeyAnalogEvent(negative_key, value < 0.0F, std::max(-value, 0.0F));
        io.AddKeyAnalogEvent(positive_key, value > 0.0F, std::max(value, 0.0F));
    };

    button(ImGuiKey_GamepadStart, SDL_CONTROLLER_BUTTON_START);
    button(ImGuiKey_GamepadBack, SDL_CONTROLLER_BUTTON_BACK);
    button(ImGuiKey_GamepadFaceDown, SDL_CONTROLLER_BUTTON_A);
    button(ImGuiKey_GamepadFaceRight, SDL_CONTROLLER_BUTTON_B);
    button(ImGuiKey_GamepadFaceLeft, SDL_CONTROLLER_BUTTON_X);
    button(ImGuiKey_GamepadFaceUp, SDL_CONTROLLER_BUTTON_Y);
    button(ImGuiKey_GamepadDpadLeft, SDL_CONTROLLER_BUTTON_DPAD_LEFT);
    button(ImGuiKey_GamepadDpadRight, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
    button(ImGuiKey_GamepadDpadUp, SDL_CONTROLLER_BUTTON_DPAD_UP);
    button(ImGuiKey_GamepadDpadDown, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    button(ImGuiKey_GamepadL1, SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
    button(ImGuiKey_GamepadR1, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
    button(ImGuiKey_GamepadL3, SDL_CONTROLLER_BUTTON_LEFTSTICK);
    button(ImGuiKey_GamepadR3, SDL_CONTROLLER_BUTTON_RIGHTSTICK);
    axis(ImGuiKey_GamepadLStickLeft, ImGuiKey_GamepadLStickRight,
         SDL_CONTROLLER_AXIS_LEFTX);
    axis(ImGuiKey_GamepadLStickUp, ImGuiKey_GamepadLStickDown,
         SDL_CONTROLLER_AXIS_LEFTY);
    axis(ImGuiKey_GamepadRStickLeft, ImGuiKey_GamepadRStickRight,
         SDL_CONTROLLER_AXIS_RIGHTX);
    axis(ImGuiKey_GamepadRStickUp, ImGuiKey_GamepadRStickDown,
         SDL_CONTROLLER_AXIS_RIGHTY);

    const float left_trigger = connected
        ? std::max(static_cast<float>(SDL_GameControllerGetAxis(
              controller, SDL_CONTROLLER_AXIS_TRIGGERLEFT)) / 32767.0F, 0.0F)
        : 0.0F;
    const float right_trigger = connected
        ? std::max(static_cast<float>(SDL_GameControllerGetAxis(
              controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT)) / 32767.0F, 0.0F)
        : 0.0F;
    io.AddKeyAnalogEvent(ImGuiKey_GamepadL2, left_trigger > 0.10F, left_trigger);
    io.AddKeyAnalogEvent(ImGuiKey_GamepadR2, right_trigger > 0.10F, right_trigger);
}

#endif

void dkr::runtime::platform::queue_audio(std::int16_t* samples,
                                         std::size_t sample_count) {
    dkr::runtime::telemetry::record_audio_buffer(sample_count);
    const auto index = ++g_audio_buffers;
#if DKR_RUNTIME_HAS_RT64
    std::scoped_lock lock(g_platform_mutex);
    if (g_audio_device != 0 && sample_count != 0) {
        const std::size_t maximum_samples =
            static_cast<std::size_t>(std::max(g_audio_frequency, 48000U)) * 2U;
        if (sample_count > maximum_samples || (sample_count & 1U) != 0) {
            std::fprintf(stderr,
                         "[boot][audio] rejected invalid buffer samples=%zu maximum=%zu\n",
                         sample_count, maximum_samples);
            return;
        }
        const std::size_t queued_frames_before =
            SDL_GetQueuedAudioSize(g_audio_device) /
            (2U * sizeof(std::int16_t));
        const std::size_t submitted_frames = sample_count / 2U;
        if (g_audio_nominal_block_frames == 0U) {
            g_audio_nominal_block_frames = submitted_frames;
        }
        const std::size_t cushion_block_frames = std::max(
            g_audio_nominal_block_frames,
            static_cast<std::size_t>(g_audio_callback_frames));
        g_audio_prime_target_frames = cushion_block_frames *
                                      static_cast<std::size_t>(g_audio_cushion_blocks);
        if (index > 1U) {
            if (queued_frames_before == 0U) {
                if (g_audio_playback_started) {
                    // The queued-audio API fills a drained device with silence.
                    // Pause immediately and rebuild a bounded cushion so a
                    // quantised cutscene refill cadence cannot produce a train
                    // of short silence gaps. This is deliberately host-only:
                    // audio_frames_remaining() continues to expose the original
                    // emulated AI DMA state to DKR.
                    SDL_PauseAudioDevice(g_audio_device, 1);
                    g_audio_playback_started = false;
                    g_audio_cushion_blocks = std::min(g_audio_cushion_blocks + 1U, 3U);
                    g_audio_prime_target_frames = cushion_block_frames *
                                                  static_cast<std::size_t>(
                                                      g_audio_cushion_blocks);
                }
            }
        }
        g_audio_swap_buffer.resize(sample_count);
        g_audio_equalizer.configure(
            g_audio_frequency,
            dkr::runtime::enhancements::modern_presentation_enabled()
                ? g_bass_gain.load(std::memory_order_relaxed) : 0.0F,
            dkr::runtime::enhancements::modern_presentation_enabled()
                ? g_mid_gain.load(std::memory_order_relaxed) : 0.0F,
            dkr::runtime::enhancements::modern_presentation_enabled()
                ? g_treble_gain.load(std::memory_order_relaxed) : 0.0F);
        for (std::size_t i = 0; i < sample_count; i += 2) {
            // RDRAM's 32-bit word swap leaves each native stereo pair in R,L
            // order. Restore conventional L,R order before sending it to SDL.
            const float volume = g_master_volume.load(std::memory_order_relaxed);
            const auto filtered = g_audio_equalizer.process(
                static_cast<float>(samples[i + 1]),
                static_cast<float>(samples[i]));
            const long left = std::lround(filtered.first * volume);
            const long right = std::lround(filtered.second * volume);
            g_audio_swap_buffer[i] = static_cast<std::int16_t>(
                std::clamp(left, -32768L, 32767L));
            g_audio_swap_buffer[i + 1] = static_cast<std::int16_t>(
                std::clamp(right, -32768L, 32767L));
        }
        const auto byte_count = static_cast<Uint32>(sample_count * sizeof(std::int16_t));
        if (SDL_QueueAudio(g_audio_device, g_audio_swap_buffer.data(), byte_count) != 0) {
            std::fprintf(stderr, "[boot][audio] queue failed: %s\n", SDL_GetError());
        } else {
            const std::size_t queued_frames = SDL_GetQueuedAudioSize(g_audio_device) /
                                              (2U * sizeof(std::int16_t));
            if (!g_audio_playback_started && g_audio_prime_target_frames != 0U &&
                queued_frames >= g_audio_prime_target_frames) {
                SDL_PauseAudioDevice(g_audio_device, 0);
                g_audio_playback_started = true;
            }
        }
    }
#else
    (void)samples;
#endif
}

float dkr::runtime::platform::master_volume() {
    return g_master_volume.load(std::memory_order_acquire);
}

void dkr::runtime::platform::set_master_volume(float volume) {
    g_master_volume.store(std::clamp(volume, 0.0F, 1.0F), std::memory_order_release);
}

float dkr::runtime::platform::bass_gain() {
    return g_bass_gain.load(std::memory_order_acquire);
}

void dkr::runtime::platform::set_bass_gain(float decibels) {
    g_bass_gain.store(dkr::runtime::audio::clamp_eq_gain(decibels),
                      std::memory_order_release);
}

float dkr::runtime::platform::mid_gain() {
    return g_mid_gain.load(std::memory_order_acquire);
}

void dkr::runtime::platform::set_mid_gain(float decibels) {
    g_mid_gain.store(dkr::runtime::audio::clamp_eq_gain(decibels),
                     std::memory_order_release);
}

float dkr::runtime::platform::treble_gain() {
    return g_treble_gain.load(std::memory_order_acquire);
}

void dkr::runtime::platform::set_treble_gain(float decibels) {
    g_treble_gain.store(dkr::runtime::audio::clamp_eq_gain(decibels),
                        std::memory_order_release);
}

std::size_t dkr::runtime::platform::audio_frames_remaining() {
#if DKR_RUNTIME_HAS_RT64
    std::scoped_lock lock(g_platform_mutex);
    if (g_audio_device != 0) {
        // SDL's queue contains one deliberately protected host-side cushion
        // block in addition to the block that represents the N64 AI's active
        // DMA. DKR's audiomgr uses osAiGetLength as feedback when selecting its
        // next 720..848-frame synthesis quantum, so exposing the cushion makes
        // every decision look artificially full and locks production to the
        // 720-frame minimum. Exclude exactly that protected block. While the
        // device is paused for initial/rebuffer priming, report an idle AI so
        // the original manager can promptly generate the blocks needed to
        // start playback.
        if (!g_audio_playback_started) {
            return 0U;
        }
        const std::size_t queued_frames = SDL_GetQueuedAudioSize(g_audio_device) /
                                          (2U * sizeof(std::int16_t));
        // DKR's retail audiomgr targets a small active-DMA residual when it
        // chooses the next synthesis quantum. At 22,050 Hz its aligned authored
        // frame is 736 samples; the initial no-DMA quantum is 848, leaving the
        // intended feedback residual at 112. Centre the host queue controller
        // on that residual rather than exposing an entire buffered block. This
        // lets DKR alternate its original 720/736/752-frame corrections around
        // the queue target instead of slowly draining the cushion.
        const std::size_t authored_frame_frames =
            (((static_cast<std::size_t>(g_audio_frequency) + 29U) / 30U) + 15U) &
            ~std::size_t{15U};
        const std::size_t target_active_residual =
            g_audio_nominal_block_frames > authored_frame_frames
                ? g_audio_nominal_block_frames - authored_frame_frames
                : 0U;
        const std::size_t protected_host_frames =
            g_audio_prime_target_frames > target_active_residual
                ? g_audio_prime_target_frames - target_active_residual
                : 0U;
        const std::size_t feedback_frames =
            queued_frames > protected_host_frames
                ? queued_frames - protected_host_frames
                : 0U;
        // osAiGetLength reports the active N64 DMA, not the sum of both AI
        // slots. DKR uses that distinction when sizing its next audio frame.
        const std::size_t one_dma_frames = std::max(g_audio_frequency / 30U, 1U);
        return std::min(feedback_frames, one_dma_frames);
    }
#endif
    return 0;
}

void dkr::runtime::platform::set_audio_frequency(std::uint32_t frequency) {
    if (frequency == 0) {
        return;
    }
#if DKR_RUNTIME_HAS_RT64
    std::scoped_lock lock(g_platform_mutex);
    if (g_audio_device != 0 && g_audio_frequency == frequency) {
        return;
    }
    if (g_audio_device != 0) {
        SDL_ClearQueuedAudio(g_audio_device);
        SDL_CloseAudioDevice(g_audio_device);
        g_audio_device = 0;
    }

    SDL_AudioSpec desired{};
    SDL_AudioSpec obtained{};
    desired.freq = static_cast<int>(frequency);
    desired.format = AUDIO_S16SYS;
    desired.channels = 2;
    // DKR normally submits 720 stereo frames per authored 30 Hz mix. A
    // 1024-frame host callback cannot be satisfied by one such buffer and
    // repeatedly inserts silence at cutscene transitions. Keep the callback
    // below the retail mix quantum; this changes host delivery granularity,
    // not DKR's emulated AI timing or sample production.
    desired.samples = 512;
    g_audio_device = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained, 0);
    if (g_audio_device == 0) {
        std::fprintf(stderr, "[boot][audio] open failed at %u Hz: %s\n",
                     frequency, SDL_GetError());
        g_audio_frequency = 0;
        return;
    }
    g_audio_frequency = static_cast<std::uint32_t>(obtained.freq);
    g_audio_callback_frames = obtained.samples;
    g_audio_playback_started = false;
    g_audio_nominal_block_frames = 0U;
    g_audio_prime_target_frames = 0U;
    g_audio_cushion_blocks = 2U;
    g_audio_equalizer.reset();
    std::fprintf(stderr, "[boot][audio] device opened requested=%u actual=%d format=%04X channels=%u\n",
                 frequency, obtained.freq, obtained.format, obtained.channels);
#else
    std::fprintf(stderr, "[boot][audio] frequency=%u (diagnostic backend)\n", frequency);
#endif
}

void dkr::runtime::platform::poll_input() {
#if DKR_RUNTIME_HAS_RT64
    std::scoped_lock lock(g_platform_mutex);
    SDL_GameControllerUpdate();
    RefreshControllers();
    const bool blocked = dkr::runtime::ui::overlay_visible();
    for (std::size_t player = 0; player < kControllerCount; ++player) {
        const auto state = dkr::runtime::input::poll(
            g_controllers[player],
            player == 0U ? g_gyro_controller : nullptr,
            player == 0U, blocked);
        g_buttons[player].store(state.buttons, std::memory_order_release);
        g_stick_x[player].store(state.stick_x, std::memory_order_release);
        g_stick_y[player].store(state.stick_y, std::memory_order_release);
    }
#else
    for (std::size_t player = 0; player < kControllerCount; ++player) {
        g_buttons[player].store(0, std::memory_order_release);
        g_stick_x[player].store(0.0F, std::memory_order_release);
        g_stick_y[player].store(0.0F, std::memory_order_release);
    }
#endif
}

bool dkr::runtime::platform::get_input(int controller, std::uint16_t* buttons,
                                       float* x, float* y) {
    if (controller < 0 || controller >= static_cast<int>(kControllerCount)) {
        return false;
    }
    const auto index = static_cast<std::size_t>(controller);
    *buttons = g_buttons[index].load(std::memory_order_acquire);
    *x = g_stick_x[index].load(std::memory_order_acquire);
    *y = g_stick_y[index].load(std::memory_order_acquire);
    return true;
}

void dkr::runtime::platform::set_rumble(int controller, bool enabled) {
#if DKR_RUNTIME_HAS_RT64
    if (controller < 0 || controller >= static_cast<int>(kControllerCount)) {
        return;
    }
    std::scoped_lock lock(g_platform_mutex);
    SDL_GameController* game_controller = g_controllers[static_cast<std::size_t>(controller)];
    if (game_controller != nullptr) {
        const bool active = enabled && g_rumble_enabled.load(std::memory_order_acquire);
        const Uint16 strength = active
            ? static_cast<Uint16>(std::lround(
                  std::clamp(g_rumble_strength.load(std::memory_order_relaxed),
                             0.0F, 1.0F) * 65535.0F))
            : 0U;
        SDL_GameControllerRumble(game_controller, strength, strength,
                                 active ? SDL_HAPTIC_INFINITY : 0U);
    }
#else
    (void)controller;
    (void)enabled;
#endif
}

float dkr::runtime::platform::rumble_strength() {
    return g_rumble_strength.load(std::memory_order_acquire);
}

void dkr::runtime::platform::set_rumble_strength(float strength) {
    g_rumble_strength.store(std::clamp(strength, 0.0F, 1.0F),
                            std::memory_order_release);
}

bool dkr::runtime::platform::rumble_enabled() {
    return g_rumble_enabled.load(std::memory_order_acquire);
}

void dkr::runtime::platform::set_rumble_enabled(bool enabled) {
    g_rumble_enabled.store(enabled, std::memory_order_release);
#if DKR_RUNTIME_HAS_RT64
    if (!enabled) {
        std::scoped_lock lock(g_platform_mutex);
        for (SDL_GameController* controller : g_controllers) {
            if (controller != nullptr) {
                SDL_GameControllerRumble(controller, 0U, 0U, 0U);
            }
        }
    }
#endif
}

bool dkr::runtime::platform::gyro_available() {
#if DKR_RUNTIME_HAS_RT64
    std::scoped_lock lock(g_platform_mutex);
    return g_gyro_controller != nullptr &&
           SDL_GameControllerGetAttached(g_gyro_controller) == SDL_TRUE &&
           SDL_GameControllerHasSensor(
               g_gyro_controller, SDL_SENSOR_GYRO) == SDL_TRUE;
#else
    return false;
#endif
}

ultramodern::input::connected_device_info_t
dkr::runtime::platform::get_connected_device_info(int controller) {
    if (controller < 0 || controller >= static_cast<int>(kControllerCount)) {
        return {ultramodern::input::Device::None, ultramodern::input::Pak::None};
    }
#if DKR_RUNTIME_HAS_RT64
    std::scoped_lock lock(g_platform_mutex);
    const bool has_gamepad = g_controllers[static_cast<std::size_t>(controller)] != nullptr;
    if (controller == 0) {
        return {ultramodern::input::Device::Controller,
                has_gamepad ? ultramodern::input::Pak::RumblePak
                            : ultramodern::input::Pak::None};
    }
    return {has_gamepad ? ultramodern::input::Device::Controller
                        : ultramodern::input::Device::None,
            has_gamepad ? ultramodern::input::Pak::RumblePak
                        : ultramodern::input::Pak::None};
#else
    return {ultramodern::input::Device::Controller, ultramodern::input::Pak::None};
#endif
}
