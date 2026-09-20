#pragma once
#include "mods/legacy_runtime_session.hpp"
#include "mods/legacy_mod_launch.hpp"

namespace dkr::runtime::legacy {
// Called with the game stopped. A null session restores zero-overhead stock
// routing. Keep the session alive until all runtime guest threads have joined.
void begin_session(std::shared_ptr<mods::RuntimeSession> session);
// Offline, validated launcher admission. Never called while guest threads run.
void begin_prepared(std::shared_ptr<const mods::PreparedModLaunch> prepared);
std::shared_ptr<const mods::PreparedModLaunch> prepared_launch();
void begin_track_menu(bool allow_races=false);
// Explicit roster seeding is reserved for development qualification.
void begin_character_roster(const std::array<std::string,4>& selected);
void begin_character_menu();
void request_scene(const std::string& id,unsigned carrier);
std::string failure();
}
