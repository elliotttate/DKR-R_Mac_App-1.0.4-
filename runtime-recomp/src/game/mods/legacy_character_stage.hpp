#pragma once
#include "legacy_character_menu_render.hpp"

namespace dkr::mods {
struct CharacterStageCalls {
    OriginalPiHandler spawn=nullptr,free=nullptr,animation_fraction=nullptr,particles=nullptr;
    OriginalPiHandler animation_override=nullptr,camera=nullptr;
};
// Menu-only actors, with independent sign counters. The original object/model
// allocator and destructor retain ownership of every guest allocation.
class CharacterStage {
public:
    void initialize(std::span<std::uint8_t>,const CharacterMenuFields&,recomp_context&,
        const CharacterStageCalls&,const std::vector<AllocatedCharacter>&);
    void release(std::span<std::uint8_t>,recomp_context&,const CharacterStageCalls&);
    bool update(std::span<std::uint8_t>,const CharacterMenuFields&,recomp_context&,
        const CharacterStageCalls&,const CharacterMenuView&);
    bool redirect_spawn(std::span<std::uint8_t>,recomp_context&);
private:
    struct Actor {std::uint32_t object=0;unsigned sign_ticks=0,sign_index=0;std::vector<unsigned> sign_batches;};
    std::vector<Actor> actors_;
    std::vector<std::uint32_t> retiring_;
    CharacterMenuFields fields_{};
    std::uint32_t pending_entry_=0,pending_header_=0;
    unsigned pending_source_=0;
};
}
