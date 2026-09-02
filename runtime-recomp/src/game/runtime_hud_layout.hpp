#pragma once

#include "hud_layout_policy.hpp"

#include <cstdint>
#include <filesystem>

struct recomp_context;

namespace dkr::runtime::hud {

void configure(const std::filesystem::path& config_directory);

LayoutMode mode();
void set_mode(LayoutMode value);
float global_scale();
void set_global_scale(float value);

void set_viewport_extent(int width, int height);

void begin_element(std::uint8_t* rdram, recomp_context* context);
void end_element(std::uint8_t* rdram, recomp_context* context);
void begin_minimap(std::uint8_t* rdram, recomp_context* context);
void end_minimap(std::uint8_t* rdram, recomp_context* context);
void begin_player_pass(std::uint8_t* rdram, recomp_context* context);
void end_player_pass(std::uint8_t* rdram, recomp_context* context);
void begin_general_pass(std::uint8_t* rdram, recomp_context* context);
void end_general_pass(std::uint8_t* rdram, recomp_context* context);
void begin_dialogue_pass(std::uint8_t* rdram, recomp_context* context);
void end_dialogue_pass(std::uint8_t* rdram, recomp_context* context);

} // namespace dkr::runtime::hud

extern "C" void dkr_hud_element_begin(std::uint8_t* rdram, recomp_context* context);
extern "C" void dkr_hud_element_end(std::uint8_t* rdram, recomp_context* context);
extern "C" void dkr_hud_minimap_begin(std::uint8_t* rdram,
                                        recomp_context* context);
extern "C" void dkr_hud_minimap_end(std::uint8_t* rdram,
                                      recomp_context* context);
extern "C" void dkr_hud_player_pass_begin(std::uint8_t* rdram,
                                            recomp_context* context);
extern "C" void dkr_hud_player_pass_end(std::uint8_t* rdram,
                                          recomp_context* context);
extern "C" void dkr_hud_general_pass_begin(std::uint8_t* rdram,
                                             recomp_context* context);
extern "C" void dkr_hud_general_pass_end(std::uint8_t* rdram,
                                           recomp_context* context);
extern "C" void dkr_hud_dialogue_pass_begin(std::uint8_t* rdram,
                                              recomp_context* context);
extern "C" void dkr_hud_dialogue_pass_end(std::uint8_t* rdram,
                                            recomp_context* context);
extern "C" void dkr_refresh_combined_accessories(std::uint8_t* rdram,
                                                   recomp_context* context);
