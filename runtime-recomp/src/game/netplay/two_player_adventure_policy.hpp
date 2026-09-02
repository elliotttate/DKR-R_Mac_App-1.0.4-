#pragma once

#include "netplay_pacing_policy.hpp"

#include <cstdint>

namespace dkr::runtime::netplay {

// DKR's JOINTVENTURE mode uses one visible overworld racer and swaps the
// retail controller-ID table when Player 2 becomes the lead. Online play must
// keep its authored Player 1/Player 2 slots stable and let the retail mapping
// consume the appropriate slot; applying a second netplay remap would invert
// the ownership twice and make the shared hub uncontrollable.
enum class TwoPlayerAdventurePhase : std::uint8_t {
    GuidedMenus,
    SharedHubPlayer1,
    SharedHubPlayer2,
    TransitionBarrier,
    DualGameplay,
};

inline constexpr TwoPlayerAdventurePhase two_player_adventure_phase(
    bool online_running, bool assigned_ports_released,
    bool two_player_adventure, bool player_two_is_lead,
    FrameDebtPhase frame_debt_phase) {
    if (!online_running ||
        frame_debt_phase == FrameDebtPhase::Inactive ||
        !assigned_ports_released) {
        return TwoPlayerAdventurePhase::GuidedMenus;
    }
    if (frame_debt_phase_is_barrier(frame_debt_phase)) {
        return TwoPlayerAdventurePhase::TransitionBarrier;
    }
    if (frame_debt_phase == FrameDebtPhase::Gameplay) {
        return TwoPlayerAdventurePhase::DualGameplay;
    }
    if (two_player_adventure) {
        return player_two_is_lead
            ? TwoPlayerAdventurePhase::SharedHubPlayer2
            : TwoPlayerAdventurePhase::SharedHubPlayer1;
    }
    return TwoPlayerAdventurePhase::GuidedMenus;
}

inline constexpr bool two_player_adventure_accepts_assigned_input(
    TwoPlayerAdventurePhase phase) {
    return phase == TwoPlayerAdventurePhase::SharedHubPlayer1 ||
           phase == TwoPlayerAdventurePhase::SharedHubPlayer2 ||
           phase == TwoPlayerAdventurePhase::DualGameplay;
}

// JOINTVENTURE deliberately exposes one overworld racer even though two
// network players remain assigned. Treat only that exact topology as a valid
// synchronized gameplay scene. Races, bosses and minigames still have to pass
// the ordinary multi-racer readiness check, so this exception cannot conceal
// an incomplete two-player track load.
inline constexpr bool two_player_adventure_shared_hub_topology_ready(
    bool online_running, bool assigned_ports_released,
    bool two_player_adventure, std::uint32_t active_players,
    std::uint32_t racer_count, std::uint8_t expected_players) {
    return online_running && assigned_ports_released &&
           two_player_adventure && expected_players == 2U &&
           active_players == 1U && racer_count >= 1U && racer_count <= 10U;
}

// On a brand-new save the first shared-hub load can finish before retail has
// latched is_in_two_player_adventure(). The synchronized JOINTVENTURE setting
// is already immutable at that point, so admit only the exact pre-latch hub
// topology. Requiring the hub race type prevents this bootstrap exception from
// masking an incomplete race, boss, challenge, or minigame roster.
inline constexpr bool two_player_adventure_bootstrap_hub_topology_ready(
    bool online_running, bool assigned_ports_released,
    bool jointventure_selected, int race_type,
    std::uint32_t active_players, std::uint32_t racer_count,
    std::uint8_t expected_players) {
    constexpr int kHubWorldRaceType = 5;
    return online_running && assigned_ports_released &&
           jointventure_selected && race_type == kHubWorldRaceType &&
           expected_players == 2U && active_players == 1U &&
           racer_count >= 1U && racer_count <= 10U;
}

} // namespace dkr::runtime::netplay
