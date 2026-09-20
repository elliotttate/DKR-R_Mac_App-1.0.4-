#pragma once
#include <chrono>
#include <filesystem>
#include <functional>
#include <string>

namespace dkr::mods {
enum class WorkerOutcome { Completed, Cancelled, TimedOut, Failed };
struct WorkerResult {WorkerOutcome outcome=WorkerOutcome::Failed; unsigned exit_code=0;};
// Runs only the application-supplied executable, with one argument. No shell.
// Call from an import job, never while holding a UI/network/session lock.
WorkerResult run_worker(const std::filesystem::path& executable,
    const std::filesystem::path& request,
    const std::function<bool()>& keep_running,
    std::chrono::milliseconds timeout=std::chrono::minutes(3));
void constrain_import_worker();
} // namespace dkr::mods
