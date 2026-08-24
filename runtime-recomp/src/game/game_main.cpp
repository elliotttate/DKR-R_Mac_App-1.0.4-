#include "game_registration.hpp"
#include "null_renderer.hpp"
#include "runtime_platform.hpp"
#include "save_manager.hpp"
#include "virtual_pak.hpp"
#if DKR_RUNTIME_HAS_RT64
#include "rt64_renderer.hpp"
#include "runtime_texture_packs.hpp"
#include "runtime_ui.hpp"
#endif

#include "librecomp/game.hpp"
#include "librecomp/rsp.hpp"
#include "ultramodern/config.hpp"
#include "ultramodern/ultramodern.hpp"

#include <atomic>
#include <chrono>
#include <cinttypes>
#include <cstdlib>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <string>
#include <string_view>
#include <thread>

#ifndef _WIN32
#include <cerrno>
#include <unistd.h>
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <DbgHelp.h>
#endif

extern RspUcodeFunc dkrAspMain;

namespace {

#ifdef _WIN32
std::atomic_flag g_crash_filter_active = ATOMIC_FLAG_INIT;
#endif

std::filesystem::path DefaultConfigDirectory(const char* executable_argument) {
    std::error_code error;
    const std::filesystem::path executable = std::filesystem::absolute(
        std::filesystem::u8path(executable_argument), error);
    const std::filesystem::path executable_directory = error
        ? std::filesystem::current_path()
        : executable.parent_path();
    if (std::filesystem::exists(executable_directory / "portable.txt")) {
        return executable_directory / "dkr-runtime-data";
    }
#if defined(_WIN32)
    if (const char* app_data = std::getenv("APPDATA"); app_data != nullptr && *app_data != '\0') {
        return std::filesystem::path(app_data) / "DKRPort";
    }
#else
    if (const char* xdg_config = std::getenv("XDG_CONFIG_HOME");
        xdg_config != nullptr && *xdg_config != '\0') {
        return std::filesystem::path(xdg_config) / "dkr-port";
    }
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
        return std::filesystem::path(home) / ".config" / "dkr-port";
    }
#endif
    return executable_directory / "dkr-runtime-data";
}

RspExitReason EmptyAudioTask(std::uint8_t*, std::uint32_t) {
    return RspExitReason::Broke;
}

RspUcodeFunc* GetRspMicrocode(const OSTask* task) {
    if (task->t.type == M_AUDTASK && task->t.ucode == 0x800D7600U) {
        // DKR can submit a zero-command audio frame when the host-reported AI
        // queue already satisfies the synthesizer's requested frame size. The
        // original scheduler treats that as completed work; entering the ABI
        // dispatcher with a zero-byte list would DMA and execute stale memory.
        if (task->t.data_size == 0) {
            return EmptyAudioTask;
        }
        return +[](std::uint8_t* rdram, std::uint32_t ucode_address) {
            return dkrAspMain(rdram, ucode_address);
        };
    }
    std::fprintf(stderr,
                 "[boot][rsp] unsupported task type=%u flags=0x%08X "
                 "ucode=0x%08X ucode_size=%u ucode_data=0x%08X data=0x%08X data_size=%u\n",
                 task->t.type, task->t.flags, task->t.ucode, task->t.ucode_size,
                 task->t.ucode_data, task->t.data_ptr, task->t.data_size);
    return nullptr;
}

void MessageBox(const char* message) {
    std::fprintf(stderr, "[boot][runtime-error] %s\n", message);
}

std::string GetThreadName(const OSThread* thread) {
    return "DKR-" + std::to_string(thread->id);
}

#ifdef _WIN32
LONG WINAPI RuntimeCrashFilter(EXCEPTION_POINTERS* exception) {
    if (g_crash_filter_active.test_and_set()) {
        Sleep(5000);
        return EXCEPTION_EXECUTE_HANDLER;
    }
    HANDLE process = GetCurrentProcess();
    SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME | SYMOPT_LOAD_LINES);
    SymInitialize(process, nullptr, TRUE);

    const DWORD64 fault_address = reinterpret_cast<DWORD64>(exception->ExceptionRecord->ExceptionAddress);
    const DWORD64 module_base = reinterpret_cast<DWORD64>(GetModuleHandleW(nullptr));
    std::fprintf(stderr, "[boot][crash] exception=0x%08lX address=0x%016llX\n",
                 exception->ExceptionRecord->ExceptionCode,
                 static_cast<unsigned long long>(fault_address));
    if (exception->ExceptionRecord->NumberParameters >= 2U) {
        const ULONG_PTR operation = exception->ExceptionRecord->ExceptionInformation[0];
        const char* operation_name = operation == 0U ? "read" :
            operation == 1U ? "write" : operation == 8U ? "execute" : "unknown";
        std::fprintf(stderr,
                     "[boot][crash] memory-operation=%s(%llu) "
                     "memory-address=0x%016llX\n",
                     operation_name,
                     static_cast<unsigned long long>(operation),
                     static_cast<unsigned long long>(
                         exception->ExceptionRecord->ExceptionInformation[1]));
    }
    std::fprintf(stderr, "[boot][crash] module-base=0x%016llX rva=0x%llX\n",
                 static_cast<unsigned long long>(module_base),
                 static_cast<unsigned long long>(fault_address - module_base));
    std::fprintf(stderr,
                 "[boot][crash] registers rcx=0x%016llX rdx=0x%016llX "
                 "r8=0x%016llX r9=0x%016llX rsp=0x%016llX\n",
                 static_cast<unsigned long long>(exception->ContextRecord->Rcx),
                 static_cast<unsigned long long>(exception->ContextRecord->Rdx),
                 static_cast<unsigned long long>(exception->ContextRecord->R8),
                 static_cast<unsigned long long>(exception->ContextRecord->R9),
                 static_cast<unsigned long long>(exception->ContextRecord->Rsp));
    std::fprintf(stderr,
                 "[boot][crash] registers rax=0x%016llX rbx=0x%016llX "
                 "rbp=0x%016llX rsi=0x%016llX rdi=0x%016llX\n",
                 static_cast<unsigned long long>(exception->ContextRecord->Rax),
                 static_cast<unsigned long long>(exception->ContextRecord->Rbx),
                 static_cast<unsigned long long>(exception->ContextRecord->Rbp),
                 static_cast<unsigned long long>(exception->ContextRecord->Rsi),
                 static_cast<unsigned long long>(exception->ContextRecord->Rdi));
    std::fprintf(stderr,
                 "[boot][crash] registers r10=0x%016llX r11=0x%016llX "
                 "r12=0x%016llX r13=0x%016llX r14=0x%016llX "
                 "r15=0x%016llX rip=0x%016llX\n",
                 static_cast<unsigned long long>(exception->ContextRecord->R10),
                 static_cast<unsigned long long>(exception->ContextRecord->R11),
                 static_cast<unsigned long long>(exception->ContextRecord->R12),
                 static_cast<unsigned long long>(exception->ContextRecord->R13),
                 static_cast<unsigned long long>(exception->ContextRecord->R14),
                 static_cast<unsigned long long>(exception->ContextRecord->R15),
                 static_cast<unsigned long long>(exception->ContextRecord->Rip));

    CONTEXT context = *exception->ContextRecord;
    STACKFRAME64 frame{};
    frame.AddrPC.Offset = context.Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = context.Rbp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = context.Rsp;
    frame.AddrStack.Mode = AddrModeFlat;

    alignas(SYMBOL_INFO) unsigned char symbol_storage[sizeof(SYMBOL_INFO) + MAX_SYM_NAME]{};
    auto* symbol = reinterpret_cast<SYMBOL_INFO*>(symbol_storage);
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = MAX_SYM_NAME;

    for (unsigned index = 0; index < 32; ++index) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, GetCurrentThread(), &frame,
                         &context, nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr) ||
            frame.AddrPC.Offset == 0) {
            break;
        }
        DWORD64 displacement = 0;
        if (SymFromAddr(process, frame.AddrPC.Offset, &displacement, symbol)) {
            std::fprintf(stderr, "[boot][crash] #%u %s+0x%llX\n", index, symbol->Name,
                         static_cast<unsigned long long>(displacement));
        } else {
            std::fprintf(stderr, "[boot][crash] #%u 0x%016llX\n", index,
                         static_cast<unsigned long long>(frame.AddrPC.Offset));
        }
        IMAGEHLP_LINE64 line{};
        line.SizeOfStruct = sizeof(line);
        DWORD line_displacement = 0;
        if (SymGetLineFromAddr64(process, frame.AddrPC.Offset, &line_displacement, &line)) {
            std::fprintf(stderr, "[boot][crash]     %s:%lu+0x%lX\n",
                         line.FileName, line.LineNumber, line_displacement);
        }
    }
    SymCleanup(process);
    return EXCEPTION_EXECUTE_HANDLER;
}

#endif

} // namespace

bool RelaunchApplication(int argc, char** argv) {
#ifdef _WIN32
    (void)argc;
    (void)argv;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    std::wstring command_line = GetCommandLineW();
    if (command_line.empty() ||
        !CreateProcessW(nullptr, command_line.data(), nullptr, nullptr, FALSE,
                        0, nullptr, nullptr, &startup, &process)) {
        std::fprintf(stderr, "[boot][restart] CreateProcessW failed: %lu\n",
                     static_cast<unsigned long>(GetLastError()));
        return false;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
#else
    if (argc <= 0 || argv == nullptr || argv[0] == nullptr) {
        return false;
    }
    std::error_code error;
    const std::filesystem::path executable =
        std::filesystem::absolute(std::filesystem::u8path(argv[0]), error);
    const std::string executable_utf8 = error
        ? std::string(argv[0])
        : executable.string();
    execv(executable_utf8.c_str(), argv);
    std::fprintf(stderr, "[boot][restart] execv failed: errno=%d\n", errno);
    return false;
#endif
}

int DkrMain(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);
#ifdef _WIN32
    SetUnhandledExceptionFilter(RuntimeCrashFilter);
#endif

    if (argc >= 2 && std::string_view(argv[1]) == "--self-test-pak") {
        const std::filesystem::path test_directory = argc >= 3
            ? std::filesystem::u8path(argv[2])
            : DefaultConfigDirectory(argv[0]) / "pak-self-test";
        std::string error;
        if (!dkr::runtime::pak::self_test(test_directory, error)) {
            std::fprintf(stderr, "[test][pak] FAILED: %s\n", error.c_str());
            return 1;
        }
        std::fprintf(stderr, "[test][pak] PASS: round-trip and backup recovery\n");
        return 0;
    }

#if DKR_RUNTIME_HAS_RT64
    if (argc == 4 && std::string_view(argv[1]) == "--self-test-rice-pack") {
        const std::filesystem::path source = std::filesystem::u8path(argv[2]);
        const std::filesystem::path test_directory = std::filesystem::u8path(argv[3]);
        dkr::runtime::texture_packs::configure(test_directory);
        std::string status;
        if (!dkr::runtime::texture_packs::import_archive(source, status)) {
            std::fprintf(stderr, "[test][rice] FAILED: %s\n", status.c_str());
            return 1;
        }
        const auto packs = dkr::runtime::texture_packs::snapshot();
        if (packs.size() != 1 || !packs.front().compatible ||
            packs.front().format != dkr::runtime::texture_packs::Format::RiceRt64) {
            std::fprintf(stderr, "[test][rice] FAILED: converted pack did not validate natively.\n");
            return 1;
        }
        const std::string pack_id = packs.front().id;
        const std::filesystem::path managed_path = packs.front().path;
        if (!dkr::runtime::texture_packs::set_hidden(pack_id, true, status) ||
            !dkr::runtime::texture_packs::snapshot().empty()) {
            std::fprintf(stderr, "[test][rice] FAILED: hide-from-list lifecycle failed: %s\n",
                         status.c_str());
            return 1;
        }
        const auto hidden_packs = dkr::runtime::texture_packs::snapshot(true);
        if (hidden_packs.size() != 1 || !hidden_packs.front().hidden) {
            std::fprintf(stderr, "[test][rice] FAILED: hidden pack was not retained for restoration.\n");
            return 1;
        }
        if (!dkr::runtime::texture_packs::set_hidden(pack_id, false, status) ||
            dkr::runtime::texture_packs::snapshot().size() != 1) {
            std::fprintf(stderr, "[test][rice] FAILED: restore-to-list lifecycle failed: %s\n",
                         status.c_str());
            return 1;
        }
        if (!dkr::runtime::texture_packs::delete_managed(pack_id, status) ||
            !dkr::runtime::texture_packs::snapshot(true).empty() ||
            std::filesystem::exists(managed_path)) {
            std::fprintf(stderr, "[test][rice] FAILED: permanent managed deletion failed: %s\n",
                         status.c_str());
            return 1;
        }
        std::fprintf(stderr,
                     "[test][rice] PASS: import, hide, restore and permanent deletion\n");
        return 0;
    }
#endif

    if (argc > 4) {
        std::fprintf(stderr,
                     "Usage: DKR-R [rom.z64] [config-directory] [timeout-seconds]\n");
        return 2;
    }

    std::filesystem::path rom_path;
    if (argc >= 2) {
        rom_path = std::filesystem::u8path(argv[1]);
    }
    const std::filesystem::path config_directory = argc >= 3
        ? std::filesystem::u8path(argv[2])
        : DefaultConfigDirectory(argv[0]);
    const unsigned timeout_seconds = argc >= 4 ? static_cast<unsigned>(std::stoul(argv[3])) : 0U;
    std::filesystem::create_directories(config_directory);
    dkr::runtime::pak::configure(config_directory);
    dkr::runtime::saves::configure(config_directory);

    if (!dkr::runtime::platform::initialise()) {
        return 4;
    }

    // N64ModernRuntime intentionally leaves its process-wide graphics options
    // value-initialized for applications with a settings frontend. Supply
    // parity-first defaults here so RT64 does not silently remain at 320x240.
    ultramodern::renderer::GraphicsConfig graphics_config{};
    graphics_config.developer_mode = false;
    graphics_config.res_option = ultramodern::renderer::Resolution::Auto;
    graphics_config.wm_option = ultramodern::renderer::WindowMode::Windowed;
    graphics_config.hr_option = ultramodern::renderer::HUDRatioMode::Original;
    graphics_config.api_option = ultramodern::renderer::GraphicsApi::Auto;
    graphics_config.ar_option = ultramodern::renderer::AspectRatio::Original;
    graphics_config.msaa_option = ultramodern::renderer::Antialiasing::None;
    graphics_config.rr_option = ultramodern::renderer::RefreshRate::Original;
    graphics_config.hpfb_option =
        ultramodern::renderer::HighPrecisionFramebuffer::Auto;
    graphics_config.rr_manual_value = 30;
    graphics_config.ds_option = 1;
    ultramodern::renderer::set_graphics_config(graphics_config);

    dkr::runtime::RegisterGame(config_directory);
#if DKR_RUNTIME_HAS_RT64
    dkr::runtime::ui::configure(config_directory);
    dkr::runtime::ui::reset_lifecycle_request();
    auto window_handle = dkr::runtime::platform::create_window();
#if defined(_WIN32) || defined(__APPLE__)
    if (window_handle.window == nullptr) {
#else
    if (window_handle == nullptr) {
#endif
        std::fprintf(stderr, "[boot][window] failed to create the DKR-R window\n");
        dkr::runtime::platform::shutdown();
        return 4;
    }
    if (rom_path.empty()) {
        const auto startup = dkr::runtime::ui::run_startup_screen(
            static_cast<SDL_Window*>(dkr::runtime::platform::sdl_window()));
        if (!startup.start_game) {
            dkr::runtime::platform::shutdown();
            if (startup.lifecycle_request ==
                dkr::runtime::ui::LifecycleRequest::Restart) {
                return RelaunchApplication(argc, argv) ? 0 : 6;
            }
            return 0;
        }
        rom_path = startup.rom_path;
        window_handle = dkr::runtime::platform::prepare_window_for_game();
#if defined(_WIN32) || defined(__APPLE__)
        if (window_handle.window == nullptr) {
#else
        if (window_handle == nullptr) {
#endif
            std::fprintf(stderr,
                         "[boot][window] failed to prepare the game renderer window\n");
            dkr::runtime::platform::shutdown();
            return 4;
        }
    }
#else
    const ultramodern::renderer::WindowHandle window_handle{};
    if (rom_path.empty()) {
        std::fprintf(stderr, "The diagnostic runtime requires a ROM path.\n");
        dkr::runtime::platform::shutdown();
        return 2;
    }
#endif

    std::string rom_error;
    if (!dkr::runtime::SelectRom(rom_path, rom_error)) {
        std::fprintf(stderr, "[boot][rom] %s\n", rom_error.c_str());
        return 3;
    }
    std::fprintf(stderr, "[boot][rom] validated and registered\n");

    const recomp::rsp::callbacks_t rsp_callbacks{.get_rsp_microcode = GetRspMicrocode};
    const ultramodern::renderer::callbacks_t renderer_callbacks{
#if DKR_RUNTIME_HAS_RT64
        .create_render_context = dkr::runtime::CreateRT64Renderer};
#else
        .create_render_context = dkr::runtime::CreateDiagnosticRenderer};
#endif
    const ultramodern::audio_callbacks_t audio_callbacks{
        .queue_samples = dkr::runtime::platform::queue_audio,
        .get_frames_remaining = dkr::runtime::platform::audio_frames_remaining,
        .set_frequency = dkr::runtime::platform::set_audio_frequency,
    };
    const ultramodern::input::callbacks_t input_callbacks{
        .poll_input = dkr::runtime::platform::poll_input,
        .get_input = dkr::runtime::platform::get_input,
        .set_rumble = dkr::runtime::platform::set_rumble,
        .get_connected_device_info = dkr::runtime::platform::get_connected_device_info,
    };
    const ultramodern::renderer::callbacks_t unused_renderer_callbacks = renderer_callbacks;
    (void)unused_renderer_callbacks;
    // SDL's window event queue is serviced explicitly on DkrMain's thread
    // below. Do not rely on librecomp's optional update callback: that path is
    // not consistently serviced by every pinned runtime configuration and can
    // leave Escape, window close and Exit to Desktop unresponsive.
    const ultramodern::gfx_callbacks_t gfx_callbacks{};
    const ultramodern::events::callbacks_t events_callbacks{};
    const ultramodern::error_handling::callbacks_t error_callbacks{.message_box = MessageBox};
    const ultramodern::threads::callbacks_t thread_callbacks{.get_game_thread_name = GetThreadName};

    const recomp::Configuration configuration{
        .project_version = {.major = 1, .minor = 0, .patch = 0,
                            .suffix = DKR_RELEASE_VERSION},
        .window_handle = window_handle,
        .rsp_callbacks = rsp_callbacks,
        .renderer_callbacks = renderer_callbacks,
        .audio_callbacks = audio_callbacks,
        .input_callbacks = input_callbacks,
        .gfx_callbacks = gfx_callbacks,
        .events_callbacks = events_callbacks,
        .error_handling_callbacks = error_callbacks,
        .threads_callbacks = thread_callbacks,
        // DKR's scheduler interrupt queue can briefly be full while the VI and
        // audio managers are active. SP/DP completion edges must be retained
        // until the scheduler accepts them or gfxtask_wait can block forever.
        // The renderer now parses from immutable submission snapshots and all
        // Release tasks complete comfortably inside DKR's watchdog, so retrying
        // a blocked edge cannot outlive the task that owns it.
        .message_queue_control = {.requeue_sp = true, .requeue_dp = true},
    };

    std::fprintf(stderr, "[boot] runtime initialized; waiting for first safe VI state\n");
    std::atomic<bool> runtime_finished{false};
    std::exception_ptr runtime_failure;
    std::thread runtime_thread([&] {
        try {
            recomp::start(configuration);
        } catch (...) {
            runtime_failure = std::current_exception();
        }
        runtime_finished.store(true, std::memory_order_release);
    });

    const auto runtime_started_at = std::chrono::steady_clock::now();
    bool timeout_requested = false;
    while (!runtime_finished.load(std::memory_order_acquire)) {
#if DKR_RUNTIME_HAS_RT64
        // The SDL video subsystem and native window were created on this
        // thread. Keep all window/input event pumping here for Windows, X11
        // and Wayland compatibility while the recompiler owns its worker.
        dkr::runtime::platform::pump_window_events(nullptr);
#endif
        if (!timeout_requested && timeout_seconds != 0 &&
            std::chrono::steady_clock::now() - runtime_started_at >=
                std::chrono::seconds(timeout_seconds)) {
#if DKR_RUNTIME_HAS_RT64
            std::fprintf(stderr, "[boot][watchdog] completed-f3ddkr-tasks=%llu\n",
                         static_cast<unsigned long long>(
                             dkr::runtime::completed_f3ddkr_task_count()));
#endif
            std::fprintf(stderr, "[boot][watchdog] stopping after %u seconds\n",
                         timeout_seconds);
            timeout_requested = true;
            ultramodern::quit();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    runtime_thread.join();
#if DKR_RUNTIME_HAS_RT64
    const auto lifecycle_request = dkr::runtime::ui::lifecycle_request();
#endif
    dkr::runtime::platform::shutdown();
    if (runtime_failure != nullptr) {
        try {
            std::rethrow_exception(runtime_failure);
        } catch (const std::exception& error) {
            std::fprintf(stderr, "[boot] runtime failed: %s\n", error.what());
        } catch (...) {
            std::fprintf(stderr, "[boot] runtime failed with an unknown exception\n");
        }
        return 5;
    }
#if DKR_RUNTIME_HAS_RT64
    if (lifecycle_request == dkr::runtime::ui::LifecycleRequest::Restart) {
        std::fprintf(stderr, "[boot][restart] relaunching DKR-R\n");
        return RelaunchApplication(argc, argv) ? 0 : 6;
    }
#endif
    std::fprintf(stderr, "[boot] runtime stopped cleanly\n");
    return 0;
}

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    return DkrMain(__argc, __argv);
}
#else
int main(int argc, char** argv) {
    return DkrMain(argc, argv);
}
#endif
