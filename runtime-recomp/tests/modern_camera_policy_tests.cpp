#include "modern_camera_policy.hpp"

#include <cmath>
#include <cstdio>

using namespace dkr::runtime::enhancements;

static_assert(clamp_fov_offset(-99) == -20);
static_assert(clamp_fov_offset(99) == 20);
static_assert(effective_gameplay_fov(PresentationProfile::Accurate, 60, 20) == 60);
static_assert(effective_gameplay_fov(PresentationProfile::Modern, 60, 10) == 70);
static_assert(effective_gameplay_fov(PresentationProfile::Modern, 45, -20) == 40);

static_assert(clamp_view_distance_multiplier(99) == 8);
static_assert(effective_view_distance(PresentationProfile::Accurate, 1000, 8) == 1000);
static_assert(effective_view_distance(PresentationProfile::Modern, 1000, 8) == 8000);
static_assert(effective_view_distance(PresentationProfile::Modern, 10000, 8) == 32767);
static_assert(effective_view_distance(PresentationProfile::Modern, -1, 8) == -1);

static_assert(effective_wave_view_distance(
    PresentationProfile::Modern, 5, kRaceTypeDefault, 3) == 5);
static_assert(effective_wave_view_distance(
    PresentationProfile::Modern, 4, kRaceTypeDefault, 3) == 3);
static_assert(effective_wave_view_distance(
    PresentationProfile::Accurate, 5, kRaceTypeDefault, 3) == 3);
static_assert(effective_wave_view_distance(
    PresentationProfile::Modern, 5, 6, 3) == 3);
static_assert(effective_wave_view_distance(
    PresentationProfile::Modern, 5, 7, 3) == 3);

static_assert(persistent_water_override_enabled(
    PresentationProfile::Modern, 2, kRaceTypeDefault));
static_assert(!persistent_water_override_enabled(
    PresentationProfile::Modern, 1, kRaceTypeDefault));
static_assert(!persistent_water_override_enabled(
    PresentationProfile::Modern, 5, 6));
static_assert(persistent_water_hq_fade(
    PresentationProfile::Modern, 2, true, 0x80) == 0);
static_assert(persistent_water_hq_fade(
    PresentationProfile::Modern, 2, false, 0x80) == 0x80);
static_assert(persistent_water_hq_fade(
    PresentationProfile::Accurate, 5, true, 0x20) == 0x20);

static_assert(scenery_retention_enabled_for_race_type(
    PresentationProfile::Modern, kRaceTypeHubWorld, true, false, false));
static_assert(scenery_retention_enabled_for_race_type(
    PresentationProfile::Modern, kRaceTypeBoss, false, true, false));
static_assert(scenery_retention_enabled_for_race_type(
    PresentationProfile::Modern, 0x40, false, false, true));
static_assert(!scenery_retention_enabled_for_race_type(
    PresentationProfile::Accurate, kRaceTypeHubWorld, true, true, true));
static_assert(!scenery_retention_enabled_for_race_type(
    PresentationProfile::Modern, 6, true, true, true));

static_assert(!relax_scenery_segment_bitfield(
    PresentationProfile::Modern, kRaceTypeHubWorld,
    SceneryRetentionMode::CurrentRegion, true, false, false));
static_assert(relax_scenery_segment_bitfield(
    PresentationProfile::Modern, kRaceTypeHubWorld,
    SceneryRetentionMode::VisibleAndAdjacent, true, false, false));
static_assert(relax_scenery_segment_bitfield(
    PresentationProfile::Modern, kRaceTypeDefault,
    SceneryRetentionMode::FullForwardView, false, true, false));
static_assert(!relax_scenery_segment_bitfield(
    PresentationProfile::Modern, kRaceTypeDefault,
    SceneryRetentionMode::FullForwardView, true, false, false));

static_assert(is_animated_scenery_behaviour(12));
static_assert(!is_animated_scenery_behaviour(4));
static_assert(is_billboard_or_effect_behaviour(3));
static_assert(!is_billboard_or_effect_behaviour(4));
static_assert(is_water_or_lava_behaviour(59));
static_assert(!is_water_or_lava_behaviour(4));
static_assert(object_view_distance_multiplier(12, 2, 4, 1, 1) == 4);
static_assert(object_view_distance_multiplier(3, 2, 1, 4, 1) == 4);
static_assert(object_view_distance_multiplier(59, 2, 1, 1, 5) == 5);
static_assert(object_view_distance_multiplier(4, 2, 4, 4, 5) == 2);

static_assert(active_viewport_aspect(16.0F / 9.0F, 0) == 16.0F / 9.0F);
static_assert(active_viewport_aspect(16.0F / 9.0F, 1) == 32.0F / 9.0F);
static_assert(active_viewport_aspect(16.0F / 9.0F, 3) == 16.0F / 9.0F);
static_assert(frustum_horizontal_scale(PresentationProfile::Accurate, true, true,
                                       32.0F / 9.0F, 0, 5) == 1.0F);
static_assert(frustum_horizontal_scale(PresentationProfile::Modern, false, true,
                                       32.0F / 9.0F, 0, 5) == 1.0F);

int main() {
    const float retention = scenery_retention_frustum_scale(
        PresentationProfile::Modern, kRaceTypeDefault,
        SceneryRetentionMode::FullForwardView, 8, false, true, false);
    const float retention_disabled = scenery_retention_frustum_scale(
        PresentationProfile::Modern, kRaceTypeDefault,
        SceneryRetentionMode::VisibleAndAdjacent, 8, false, true, false);
    const float scale_4_3 = frustum_horizontal_scale(
        PresentationProfile::Modern, true, true, 4.0F / 3.0F, 0, 5);
    const float scale_16_9 = frustum_horizontal_scale(
        PresentationProfile::Modern, true, true, 16.0F / 9.0F, 0, 5);
    const float scale_21_9 = frustum_horizontal_scale(
        PresentationProfile::Modern, true, true, 21.0F / 9.0F, 0, 5);
    const float scale_32_9 = frustum_horizontal_scale(
        PresentationProfile::Modern, true, true, 32.0F / 9.0F, 0, 5);
    const float scale_split = frustum_horizontal_scale(
        PresentationProfile::Modern, true, true, 16.0F / 9.0F, 1, 5);
    if (std::abs(retention - 1.56F) > 0.0001F ||
        std::abs(retention_disabled - 1.0F) > 0.0001F ||
        std::abs(scale_4_3 - 1.05F) > 0.0001F ||
        std::abs(scale_16_9 - 1.4F) > 0.0001F ||
        std::abs(scale_21_9 - 1.8375F) > 0.0001F ||
        std::abs(scale_32_9 - 2.8F) > 0.0001F ||
        std::abs(scale_split - 2.8F) > 0.0001F) {
        std::fputs("[test][modern-camera-policy] FAIL\n", stderr);
        return 1;
    }
    std::puts("[test][modern-camera-policy] PASS");
    return 0;
}
