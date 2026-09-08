#include "palm_model.hpp"
#include "presentation_identity.hpp"
#include "interpolation_state_policy.hpp"
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

using namespace dkr::runtime::palm;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::abort(); } } while (0)

int main(int argc, char** argv) {
    for (unsigned variant = 26; variant <= 31; ++variant) {
        CHECK(dkr::runtime::interpolation::is_layout_marker(0, variant));
        CHECK(dkr::runtime::interpolation::is_layout_marker(1, variant));
        CHECK(!dkr::runtime::interpolation::is_layout_marker(6, variant));
        CHECK(!dkr::runtime::interpolation::is_layout_marker(4, variant));
    }
    std::vector<std::uint8_t> bytes{'D','K','R','P','M','0','0','1'};
    const auto append = [&](std::uint32_t n) { for (int s = 0; s < 32; s += 8) bytes.push_back(n >> s); };
    append(3); append(3);
    for (int i = 0; i < 3; ++i) {
        for (float f : {float(i) / 2, float(i) / 2, 0.F, 0.F, 1.F, 0.F, 0.F, 0.F}) append(std::bit_cast<std::uint32_t>(f));
    }
    append(0); append(1); append(2);
    Mesh mesh;
    std::string error;
    CHECK(parse_mesh(bytes, mesh, error));
    CHECK(mesh.vertices.size() == 3 && mesh.indices.size() == 3);
    const auto valid = bytes;
    bytes[0] = 0; CHECK(!parse_mesh(bytes, mesh, error)); bytes = valid;
    bytes.pop_back(); CHECK(!parse_mesh(bytes, mesh, error)); bytes = valid;
    bytes.push_back(0); CHECK(!parse_mesh(bytes, mesh, error)); bytes = valid;
    bytes[8] = 0xFF; bytes[9] = 0xFF; CHECK(!parse_mesh(bytes, mesh, error)); bytes = valid;
    bytes[bytes.size() - 4] = 3; CHECK(!parse_mesh(bytes, mesh, error)); bytes = valid;
    bytes[18] = 0xC0; bytes[19] = 0x7F; CHECK(!parse_mesh(bytes, mesh, error));
    CHECK(mesh.indices.size() == 3); // Rejected input never partially replaces a good mesh.
    Sample sample{{10,20,30}, 2, 0, 0, 0, 123, true};
    CHECK(valid_sample(sample));
    Vertex base{{0,0,0}}, top{{0,1,0}}, side{{1,0,0}};
    CHECK(std::abs(world_position(base, sample)[1] - 24.F) < 0.001F);
    CHECK(std::abs(world_position(top, sample)[1] - 156.F) < 0.001F);
    sample.yaw = 16384;
    CHECK(std::abs(world_position(side, sample)[0] - 10.F) < 0.001F);
    CHECK(std::abs(world_position(side, sample)[2] + 102.F) < 0.001F);
    sample.scale = std::numeric_limits<float>::quiet_NaN(); CHECK(!valid_sample(sample));
    sample.scale = 1; sample.position[0] = 40000; CHECK(!valid_sample(sample));
    sample.position[0] = 0; sample.identity = 0; CHECK(!valid_sample(sample));
    CHECK(supported_sprite(114) && supported_sprite(115) && supported_sprite(116));
    CHECK(supported_sprite(113) && !supported_sprite(117));
    CHECK(supported_sprite(kBlueberrySpriteId));
    CHECK(supported_sprite(kBeachTreeSpriteId) && !supported_sprite(109));
    CHECK(texture_hash(kBlueberrySpriteId) == kBlueberryTextureHash);
    CHECK(texture_hash(114) == kTextureHash && texture_hash(115) == kTextureHash);
    CHECK(texture_hash(109) == 0 && kBlueberryTextureHash != kTextureHash);
    CHECK(texture_hash(kBeachTreeSpriteId) == kBeachTreeTextureHash);
    CHECK(kBeachTreeTextureHash != kTextureHash && kBeachTreeTextureHash != kBlueberryTextureHash &&
          kBeachTreeTextureHash != kRubberTreeTextureHash);
    CHECK(texture_hash(113) == kRubberTreeTextureHash && kRubberTreeTextureHash != kTextureHash);
    CHECK(kRubberTreeTextureHash != kBlueberryTextureHash);
    sample.identity = 123; sample.yaw = 0; sample.sprite_id = 114;
    CHECK(valid_sample(sample));
    CHECK(std::abs(world_position(top, sample)[1] - 89.5F) < 0.001F);
    sample.sprite_id = 116;
    CHECK(std::abs(world_position(top, sample)[1] - 50.F) < 0.001F);
    Vertex join{{0, 0.50F, 0}};
    CHECK(std::abs(world_position(join, sample)[1] - sample.position[1]) < 0.001F);
    const std::array<std::array<float, 3>, 6> trunk{{
        {1863,388,2196}, {1878,389,2192}, {1867,378,2190},
        {1863,388,2196}, {1860,294,2250}, {1762,391,1907}}};
    sample.position = {1865,417,2197}; sample.scale = 4.546875F;
    CHECK(find_trunk_tip(trunk, sample.position, sample.attachment));
    sample.attachment_valid = true;
    CHECK(std::abs(sample.attachment[0] - 1869.3333F) < .01F);
    CHECK(std::abs(sample.attachment[1] - 385.F) < .01F);
    Vertex cut_center{{-.010F, .50F, .0025F}};
    const auto connected = world_position(cut_center, sample);
    CHECK(std::abs(connected[0] - sample.attachment[0]) < .01F);
    CHECK(std::abs(connected[1] - 382.F) < .01F);
    CHECK(std::abs(connected[2] - sample.attachment[2]) < .01F);
    CHECK(!find_trunk_tip(trunk, {0,0,0}, sample.attachment));
    CHECK(!find_trunk_tip(std::span(trunk).first(2), sample.position, sample.attachment));
    sample.attachment_valid = false; sample.position = {0,20,30}; sample.scale = 1;
    sample.sprite_id = kBlueberrySpriteId;
    CHECK(valid_sample(sample));
    CHECK(std::abs(world_position(base, sample)[1] - sample.position[1]) < 0.001F);
    CHECK(std::abs(world_position(top, sample)[1] - 133.F) < 0.001F);
    sample.yaw = 16384;
    CHECK(std::abs(world_position(side, sample)[2] + 83.F) < 0.001F);
    sample.yaw = 0;
    sample.sprite_id = kRubberTreeSpriteId;
    CHECK(valid_sample(sample));
    CHECK(world_position(base, sample) == sample.position);
    CHECK(std::abs(world_position(top, sample)[1] - 135.F) < .001F);
    sample.sprite_id = kBeachTreeSpriteId;
    CHECK(valid_sample(sample));
    CHECK(std::abs(world_position(base, sample)[1] - 14.F) < .001F);
    CHECK(std::abs(world_position(top, sample)[1] - 136.F) < .001F);
    sample.yaw = 16384;
    CHECK(std::abs(world_position(side, sample)[2] + 92.F) < .001F);
    sample.yaw = 0;
    sample.sprite_id = 109; CHECK(!valid_sample(sample));
    Assets families;
    CHECK(select_mesh(families, kBlueberrySpriteId, false) == nullptr);
    families.ready = true;
    CHECK(select_mesh(families, 115, false) == &families.near_mesh);
    CHECK(select_mesh(families, 114, true) == &families.canopy_far);
    CHECK(select_mesh(families, kBlueberrySpriteId, false) == nullptr);
    families.ready = false;
    families.blueberry_ready = true;
    CHECK(select_mesh(families, 115, false) == nullptr);
    CHECK(select_mesh(families, kBlueberrySpriteId, false) == &families.blueberry_near);
    CHECK(select_mesh(families, kBlueberrySpriteId, true) == &families.blueberry_far);
    CHECK(select_mesh(families, 107, false) == nullptr);
    CHECK(select_mesh(families, 113, false) == nullptr);
    families.rubber_tree_ready = true;
    families.blueberry_ready = false;
    CHECK(select_mesh(families, 108, false) == nullptr);
    CHECK(select_mesh(families, 113, false) == &families.rubber_tree_near);
    CHECK(select_mesh(families, 113, true) == &families.rubber_tree_far);
    CHECK(select_mesh(families, kBeachTreeSpriteId, false) == nullptr);
    families.rubber_tree_ready = false;
    families.beach_tree_ready = true;
    CHECK(select_mesh(families, kBeachTreeSpriteId, false) == &families.beach_tree_near);
    CHECK(select_mesh(families, kBeachTreeSpriteId, true) == &families.beach_tree_far);
    CHECK(select_mesh(families, 113, false) == nullptr);
    CHECK(select_mesh(families, 108, false) == nullptr);
    CHECK(select_mesh(families, 115, false) == nullptr);
    CHECK(select_mesh(families, 106, false) == nullptr);

    // Regression: a detail switch changes only the mesh, not the temporal
    // owner. Vertex interpolation must remain off even for equal-size LODs:
    // equal cardinality does not imply corresponding vertex order.
    CHECK(!kInterpolateMeshVertices);
    for (const std::uint16_t sprite : {107, 108, 113, 114, 115, 116}) {
        Sample pose{{1865, 417, 2197}, 4.546875F, 12000, -3000, 2000, 123, true};
        pose.sprite_id = sprite;
        pose.attachment = {1869.3333F, 385.F, 2192.6667F};
        pose.attachment_valid = sprite == 114 || sprite == 116;
        const auto identity = model_interpolation_key(pose);
        const auto transform = model_transform(pose);
        for (const Vertex vertex : {base, top, side, cut_center,
                                   Vertex{{-.375F, .625F, .25F}}, Vertex{{-2.F, 2.F, 2.F}}}) {
            const auto local = local_position(vertex);
            const auto expected = world_position(vertex, pose);
            for (unsigned component = 0; component < 3; ++component) {
                float actual = transform[3][component];
                float quantization_bound = .003F;
                for (unsigned axis = 0; axis < 3; ++axis) {
                    actual += local[axis] * transform[axis][component];
                    quantization_bound += std::abs(transform[axis][component]) * .5F;
                }
                CHECK(std::abs(actual - expected[component]) <= quantization_bound);
            }
        }
        // Simulate camera-distance/LOD crossings in both directions. A
        // changed object pose continues matching; a changed lifetime does not.
        for (const float depth : {1799.F, 1801.F, 1799.F, -1801.F}) {
            pose.position[2] = depth;
            CHECK(model_interpolation_key(pose) == identity);
        }
        pose.identity++;
        CHECK(model_interpolation_key(pose) != identity);
        pose.identity--;
        pose.sprite_id++;
        CHECK(model_interpolation_key(pose) != identity);
    }
    // Moving a rigid tree interpolates its transform, not a quantized
    // world-space vertex stream. Midpoint placement remains source-faithful.
    Sample from{{10,20,30}, 2, 32700, -3000, 2000, 123, true};
    Sample to = from;
    to.position = {18,26,14}; to.yaw = -32700; to.roll = 4000;
    const auto from_matrix = model_transform(from), to_matrix = model_transform(to);
    const auto local_side = local_position(side);
    const auto from_side = world_position(side, from), to_side = world_position(side, to);
    for (unsigned component = 0; component < 3; ++component) {
        float midpoint = (from_matrix[3][component] + to_matrix[3][component]) * .5F;
        for (unsigned axis = 0; axis < 3; ++axis)
            midpoint += local_side[axis] * (from_matrix[axis][component] + to_matrix[axis][component]) * .5F;
        CHECK(std::abs(midpoint - (from_side[component] + to_side[component]) * .5F) < .003F);
    }
    dkr::runtime::presentation::PresentationMarker original;
    original.palm = sample;
    auto submitted = original;
    original.palm.position[0] = 1234;
    CHECK(submitted.palm.position[0] == 0); // Sidecar holds values, not live guest pointers.
    if (argc >= 2 && std::filesystem::exists(std::filesystem::path(argv[1]) / "near.dkrmesh")) {
        initialise(argv[1], argc >= 3 ? argv[2] : "", argc >= 4 ? argv[3] : "", argc >= 5 ? argv[4] : "");
        CHECK(available() && enabled());
        CHECK(assets().near_mesh.indices.size() <= 60006);
        CHECK(assets().far_mesh.indices.size() <= 15006);
        CHECK(!assets().canopy_near.indices.empty() && !assets().canopy_far.indices.empty());
        if (argc >= 3 && std::filesystem::exists(std::filesystem::path(argv[2]) / "near.dkrmesh")) {
            CHECK(assets().blueberry_ready);
            CHECK(assets().blueberry_near.indices.size() == 3107U * 3U);
            CHECK(assets().blueberry_far.indices.size() == 3107U * 3U);
            CHECK(select_mesh(assets(), kBlueberrySpriteId, false) == &assets().blueberry_near);
            CHECK(select_mesh(assets(), 115, false) == &assets().near_mesh);
        }
        if (argc >= 4 && std::filesystem::exists(std::filesystem::path(argv[3]) / "near.dkrmesh")) {
            CHECK(assets().rubber_tree_ready);
            CHECK(assets().rubber_tree_near.indices.size() <= 6000U * 3U);
            CHECK(assets().rubber_tree_far.indices == assets().rubber_tree_near.indices);
        }
        if (argc >= 5 && std::filesystem::exists(std::filesystem::path(argv[4]) / "near.dkrmesh")) {
            CHECK(assets().beach_tree_ready);
            CHECK(assets().beach_tree_near.indices.size() == 8090U * 3U);
            CHECK(assets().beach_tree_far.indices == assets().beach_tree_near.indices);
            CHECK(select_mesh(assets(), kBeachTreeSpriteId, false) == &assets().beach_tree_near);
            CHECK(select_mesh(assets(), kBlueberrySpriteId, false) == &assets().blueberry_near);
        }
        set_enabled(false); CHECK(!enabled()); set_enabled(true); CHECK(enabled());
    }
    std::puts("Plant mesh bounds, parser, transforms, families, snapshot and fallback tests passed.");
}
