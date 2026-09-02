#include "runtime_magic_codes.hpp"

#include "magic_code_policy.hpp"
#include "revision_addresses.hpp"

#include "recomp.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#endif

#include <atomic>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <system_error>

namespace {

const std::uint32_t& kActiveMagicCodesAddress =
    dkr::runtime::revision_addresses::ActiveMagicCodes;
const std::uint32_t& kUnlockedMagicCodesAddress =
    dkr::runtime::revision_addresses::UnlockedMagicCodes;

std::atomic<std::uint32_t> g_persistent_mask{0U};
std::atomic<std::uint32_t> g_queued_one_shot_mask{0U};
std::atomic<bool> g_applied{false};
std::filesystem::path g_one_shot_path;
std::mutex g_queue_file_guard;

gpr RdramAddress(std::uint32_t address) {
    return static_cast<gpr>(static_cast<std::int32_t>(address));
}
bool ReplaceQueueFile(const std::filesystem::path& temporary,
                      const std::filesystem::path& destination) {
#if defined(_WIN32)
    return MoveFileExW(temporary.c_str(), destination.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    std::error_code error;
    std::filesystem::rename(temporary, destination, error);
    return !error;
#endif
}

bool PersistOneShotQueue(std::uint32_t mask, std::string& error) {
    std::lock_guard lock(g_queue_file_guard);
    if (g_one_shot_path.empty()) {
        error = "Magic Code launch queue is not configured yet.";
        return false;
    }
    std::error_code filesystem_error;
    if (mask == 0U) {
        std::filesystem::remove(g_one_shot_path, filesystem_error);
        if (filesystem_error) {
            error = "Could not clear the Magic Code launch queue: " +
                filesystem_error.message();
            return false;
        }
        return true;
    }

    std::filesystem::create_directories(g_one_shot_path.parent_path(),
                                        filesystem_error);
    if (filesystem_error) {
        error = "Could not create the Magic Code settings folder: " +
            filesystem_error.message();
        return false;
    }
    const auto temporary = g_one_shot_path.string() + ".tmp";
    {
        std::ofstream output(temporary, std::ios::trunc);
        output << mask << '\n';
        output.flush();
        if (!output) {
            error = "Could not write the Magic Code launch queue.";
            std::filesystem::remove(temporary, filesystem_error);
            return false;
        }
    }
    if (!ReplaceQueueFile(temporary, g_one_shot_path)) {
        error = "Could not safely replace the Magic Code launch queue.";
        std::filesystem::remove(temporary, filesystem_error);
        return false;
    }
    return true;
}

void RemoveConsumedQueueFile() {
    std::lock_guard lock(g_queue_file_guard);
    if (g_one_shot_path.empty()) {
        return;
    }
    std::error_code error;
    std::filesystem::remove(g_one_shot_path, error);
    if (error) {
        std::fprintf(stderr,
                     "[boot][magic-codes] could not clear one-shot queue: %s\n",
                     error.message().c_str());
    }
}

} // namespace

void dkr::runtime::magic_codes::configure(
    const std::filesystem::path& config_directory) {
    std::lock_guard lock(g_queue_file_guard);
    g_one_shot_path = config_directory / "magic-codes-next-launch.txt";
    std::ifstream input(g_one_shot_path);
    std::uint32_t mask = 0U;
    if (input) {
        unsigned long long parsed = 0U;
        input >> parsed;
        if (input && parsed <= 0xFFFFFFFFULL) {
            mask = static_cast<std::uint32_t>(parsed) & kOneShotMagicCodeMask;
        } else {
            std::fprintf(stderr,
                         "[boot][magic-codes] ignored malformed one-shot queue\n");
        }
    }
    g_queued_one_shot_mask.store(mask, std::memory_order_release);
    g_applied.store(false, std::memory_order_release);
}

std::uint32_t dkr::runtime::magic_codes::persistent_mask() {
    return g_persistent_mask.load(std::memory_order_acquire);
}

void dkr::runtime::magic_codes::set_persistent_mask(std::uint32_t mask) {
    g_persistent_mask.store(
        normalise_magic_code_mask(mask) & kPersistentMagicCodeMask,
        std::memory_order_release);
}

std::uint32_t dkr::runtime::magic_codes::queued_one_shot_mask() {
    return g_queued_one_shot_mask.load(std::memory_order_acquire);
}

bool dkr::runtime::magic_codes::set_queued_one_shot_mask(
    std::uint32_t mask, std::string& error) {
    mask &= kOneShotMagicCodeMask;
    if (!PersistOneShotQueue(mask, error)) {
        return false;
    }
    g_queued_one_shot_mask.store(mask, std::memory_order_release);
    return true;
}

std::uint32_t dkr::runtime::magic_codes::selected_mask() {
    return persistent_mask() | queued_one_shot_mask();
}

bool dkr::runtime::magic_codes::set_enabled(
    std::uint8_t internal_index, bool enabled, std::string& error) {
    const std::uint32_t bit = magic_code_bit(internal_index);
    if ((bit & kSelectableMagicCodeMask) == 0U) {
        error = "That Magic Code is not available in the retail game.";
        return false;
    }
    if ((bit & kOneShotMagicCodeMask) != 0U) {
        std::uint32_t next = queued_one_shot_mask();
        next = enabled ? enable_magic_code(next, internal_index)
                       : disable_magic_code(next, internal_index);
        return set_queued_one_shot_mask(next, error);
    }

    std::uint32_t next = persistent_mask();
    next = enabled ? enable_magic_code(next, internal_index)
                   : disable_magic_code(next, internal_index);
    set_persistent_mask(next);
    return true;
}

bool dkr::runtime::magic_codes::clear_all(std::string& error) {
    if (!set_queued_one_shot_mask(0U, error)) {
        return false;
    }
    set_persistent_mask(0U);
    return true;
}

extern "C" void dkr_apply_launch_magic_codes(std::uint8_t* rdram,
                                               recomp_context*) {
    bool expected = false;
    if (!g_applied.compare_exchange_strong(expected, true,
                                            std::memory_order_acq_rel)) {
        return;
    }

    const std::uint32_t persistent =
        dkr::runtime::magic_codes::persistent_mask();
    const std::uint32_t one_shot =
        g_queued_one_shot_mask.exchange(0U, std::memory_order_acq_rel);
    const std::uint32_t selected =
        dkr::runtime::magic_codes::normalise_magic_code_mask(
            persistent | one_shot);
    if (selected != 0U) {
        const gpr active = RdramAddress(kActiveMagicCodesAddress);
        const gpr unlocked = RdramAddress(kUnlockedMagicCodesAddress);
        MEM_W(0, active) = static_cast<std::uint32_t>(MEM_W(0, active)) | selected;
        MEM_W(0, unlocked) =
            static_cast<std::uint32_t>(MEM_W(0, unlocked)) | selected;
        std::fprintf(stderr,
                     "[boot][magic-codes] applied launch mask=0x%08X\n",
                     static_cast<unsigned>(selected));
    } else {
        std::fprintf(stderr, "[boot][magic-codes] no launch codes selected\n");
    }
    if (one_shot != 0U) {
        RemoveConsumedQueueFile();
    }
}

