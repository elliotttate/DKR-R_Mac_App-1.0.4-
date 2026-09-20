#pragma once
#include "legacy_character_roster.hpp"
#include "recomp.h"

namespace dkr::mods {
// The caller serializes its logical roster. No allocator, renderer, network,
// disk I/O, mutex or guest scheduler callback is used by this adapter.
void dispatch_character_event(CharacterRoster& roster,std::span<std::uint8_t> guest,
    recomp_context& ctx,unsigned event,std::uint32_t roster_address);
}
