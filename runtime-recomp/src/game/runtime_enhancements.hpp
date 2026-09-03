#pragma once

#include "modern_camera_policy.hpp"

namespace dkr::runtime::enhancements {

bool maximum_detail_enabled();
bool maximum_detail_requested();
void set_maximum_detail_enabled(bool enabled);

PresentationProfile presentation_profile();
void set_presentation_profile(PresentationProfile profile);
bool modern_presentation_enabled();
bool multiplayer_race_music_requested();
bool multiplayer_race_music_enabled();
void set_multiplayer_race_music_enabled(bool enabled);

int fov_offset();
void set_fov_offset(int offset);
int view_distance_multiplier();
void set_view_distance_multiplier(int multiplier);
bool keep_hub_scenery_requested();
bool keep_hub_scenery_enabled();
void set_keep_hub_scenery_enabled(bool enabled);
bool keep_track_scenery_requested();
bool keep_track_scenery_enabled();
void set_keep_track_scenery_enabled(bool enabled);
bool keep_minigame_scenery_requested();
bool keep_minigame_scenery_enabled();
void set_keep_minigame_scenery_enabled(bool enabled);
SceneryRetentionMode scenery_retention_mode();
void set_scenery_retention_mode(SceneryRetentionMode mode);
int animated_scenery_distance_multiplier();
void set_animated_scenery_distance_multiplier(int multiplier);
int billboard_effect_distance_multiplier();
void set_billboard_effect_distance_multiplier(int multiplier);
int water_lava_detail_multiplier();
void set_water_lava_detail_multiplier(int multiplier);
bool extended_culling_requested();
bool extended_culling_enabled();
void set_extended_culling_enabled(bool enabled);
int frustum_guard_percent();
void set_frustum_guard_percent(int percent);
bool fit_to_window_enabled();
void set_fit_to_window_enabled(bool enabled);
int anisotropy_level();
void set_anisotropy_level(int level);
float texture_lod_bias();
float effective_texture_lod_bias();
int texture_lod_bias_hundredths();
void set_texture_lod_bias_hundredths(int bias);

} // namespace dkr::runtime::enhancements
