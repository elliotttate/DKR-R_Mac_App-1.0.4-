#include "crash_log.hpp"
#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <fcntl.h>
#include <mach-o/dyld.h>
#include <string>
#include <sys/ucontext.h>
#include <unistd.h>
#include <vector>

#ifndef DKR_RELEASE_VERSION
#define DKR_RELEASE_VERSION "test"
#endif

namespace dkr::macos {
namespace {
int crash_fd = -1;
std::uintptr_t image_base = 0;

void prune(const std::filesystem::path& directory, std::size_t keep) {
    std::error_code error;
    std::vector<std::filesystem::path> files;
    for (std::filesystem::directory_iterator it(directory, error), end;
         !error && it != end; it.increment(error)) {
        if (it->is_regular_file(error) && it->path().extension() == ".log")
            files.push_back(it->path());
    }
    std::sort(files.begin(), files.end());
    for (std::size_t i = 0; i + keep < files.size(); ++i)
        std::filesystem::remove(files[i], error);
}

std::string stamp() {
    return std::to_string(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count()) + "-" +
        std::to_string(getpid());
}

void crash_handler(int signal_number, siginfo_t* info, void* context) {
    // Only async-signal-safe operations run here. Do not unwind, allocate,
    // lock, or call fprintf here: the crash may have interrupted those operations.
    char message[512];
    std::size_t used = 0;
    auto append = [&](const char* text) {
        while (*text && used < sizeof(message)) message[used++] = *text++;
    };
    auto hex = [&](std::uintptr_t value) {
        append("0x");
        for (int shift = sizeof(value) * 8 - 4; shift >= 0; shift -= 4)
            message[used++] = "0123456789abcdef"[(value >> shift) & 15];
    };
    append("DKR-R " DKR_RELEASE_VERSION " fatal signal\nsignal=");
    hex(signal_number);
    append("\nfault_address="); hex(reinterpret_cast<std::uintptr_t>(info->si_addr));
    append("\nimage_base="); hex(image_base);
    auto* state = static_cast<ucontext_t*>(context);
#if defined(__aarch64__)
    append("\npc="); hex(state->uc_mcontext->__ss.__pc);
    append("\nlr="); hex(state->uc_mcontext->__ss.__lr);
#elif defined(__x86_64__)
    append("\npc="); hex(state->uc_mcontext->__ss.__rip);
#endif
    append("\nmacOS may also save a report in DiagnosticReports.\n");
    if (crash_fd >= 0) (void)write(crash_fd, message, used);
    (void)write(STDERR_FILENO, message, used);
    // SA_RESETHAND restored SIG_DFL. Unblock and raise on this same thread so
    // Apple's report retains the failing thread rather than a random peer.
    // Never resume the instruction that caused a synchronous hardware fault.
    sigset_t unblocked;
    sigemptyset(&unblocked);
    sigaddset(&unblocked, signal_number);
    sigprocmask(SIG_UNBLOCK, &unblocked, nullptr);
    raise(signal_number);
    _exit(128 + signal_number);  // Only if signal delivery unexpectedly fails.
}
}

void install_crash_logging(const std::filesystem::path& directory, bool enabled) {
    if (enabled) {
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        if (!error) {
            // Successful launches leave an empty reserved file. Do not let
            // those displace evidence from actual crashes on later launches.
            for (std::filesystem::directory_iterator it(directory, error), end;
                 !error && it != end; it.increment(error)) {
                if (it->is_regular_file(error) && it->path().filename().string().starts_with("crash-") &&
                    it->path().extension()==".log" && it->file_size(error)==0)
                    std::filesystem::remove(it->path(), error);
            }
            prune(directory, 7);
            const auto file = directory / ("crash-" + stamp() + ".log");
            crash_fd = open(file.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
        }
    }
    image_base = reinterpret_cast<std::uintptr_t>(_dyld_get_image_header(0));
    struct sigaction action {};
    action.sa_sigaction = crash_handler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_SIGINFO | SA_RESETHAND;
    for (int signal_number : {SIGSEGV, SIGBUS, SIGABRT, SIGFPE, SIGILL, SIGTRAP})
        sigaction(signal_number, &action, nullptr);
}

void archive_runtime_log(const std::filesystem::path& directory) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(directory / "runtime.log", error)) return;
    const auto history = directory / "history";
    std::filesystem::create_directories(history, error);
    if (error) return;
    std::filesystem::copy_file(directory / "runtime.log",
        history / ("runtime-" + stamp() + ".log"), error);
    prune(history, 8);
}
}
