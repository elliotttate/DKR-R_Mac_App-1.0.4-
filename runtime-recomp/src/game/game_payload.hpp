#pragma once

#include "rom_revision.hpp"

#include "recomp.h"

#include <cstdint>

namespace dkr::runtime {

using RecompiledEntrypoint = void (*)(std::uint8_t*, recomp_context*);

struct GamePayload {
    rom::Revision revision;
    std::uint64_t rom_hash;
    RecompiledEntrypoint entrypoint;
    RecompiledEntrypoint music_volume_set;
    RecompiledEntrypoint main_game_loop;
    RecompiledEntrypoint get_settings;
    RecompiledEntrypoint leveltable_type;
    RecompiledEntrypoint sound_clear_delayed;
    RecompiledEntrypoint reset_delayed_text;
    RecompiledEntrypoint should_check_lead_player;
    RecompiledEntrypoint is_in_two_player_adventure;
    RecompiledEntrypoint is_player_two_in_control;
    RecompiledEntrypoint swap_lead_player;
    void (*register_sections)();
};

const GamePayload& payload_v77();
const GamePayload& payload_v80();
const GamePayload* payload_for(rom::Revision revision);
bool select_payload(rom::Revision revision);
const GamePayload* active_payload();
bool invoke_music_volume_set(std::uint8_t* rdram, recomp_context* context);
bool invoke_main_game_loop(std::uint8_t* rdram, recomp_context* context);

} // namespace dkr::runtime
