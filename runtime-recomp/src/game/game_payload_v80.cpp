#include "game_payload.hpp"

#include "funcs.h"
#include "librecomp/overlays.hpp"

#include "recomp_overlays.inl"

namespace {

void RegisterSectionsV80() {
    const recomp::overlays::overlay_section_table_data_t sections{
        .code_sections = section_table,
        .num_code_sections = ARRLEN(section_table),
        .total_num_sections = num_sections,
    };
    const recomp::overlays::overlays_by_index_t overlays{
        .table = overlay_sections_by_index,
        .len = ARRLEN(overlay_sections_by_index),
    };
    recomp::overlays::register_overlays(sections, overlays);
}

} // namespace

const dkr::runtime::GamePayload& dkr::runtime::payload_v80() {
    static const GamePayload payload{
        .revision = rom::Revision::UsV80,
        .rom_hash = rom::kUsV80Xxh3,
        .entrypoint = recomp_entrypoint,
        .music_volume_set = music_volume_set,
        .main_game_loop = main_game_loop,
        .get_settings = get_settings,
        .leveltable_type = leveltable_type,
        .sound_clear_delayed = sound_clear_delayed,
        .reset_delayed_text = reset_delayed_text,
        .should_check_lead_player = func_80023568,
        .is_in_two_player_adventure = is_in_two_player_adventure,
        .is_player_two_in_control = is_player_two_in_control,
        .swap_lead_player = swap_lead_player,
        .register_sections = RegisterSectionsV80,
    };
    return payload;
}
