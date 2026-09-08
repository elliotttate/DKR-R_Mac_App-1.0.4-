#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace dkr::runtime::palm {
inline constexpr std::uint64_t kTextureHash = 0x50414C4D33440001ULL;
inline constexpr std::uint64_t kBlueberryTextureHash = 0x424C554542330001ULL;
inline constexpr std::uint64_t kRubberTreeTextureHash = 0x5255424252330001ULL;
inline constexpr std::uint64_t kBeachTreeTextureHash = 0x4245414348330001ULL;
inline constexpr std::uint32_t kTextureSize = 512U; // Virtual tile; atlas is 2048².
inline constexpr std::uint32_t kSpriteId = 115U;
inline constexpr std::uint32_t kBlueberrySpriteId = 108U;
inline constexpr std::uint32_t kRubberTreeSpriteId = 113U;
inline constexpr std::uint32_t kBeachTreeSpriteId = 107U;
constexpr bool supported_sprite(std::uint32_t id) {
    return id == kBeachTreeSpriteId || id == kBlueberrySpriteId ||
        (id >= kRubberTreeSpriteId && id <= 116U);
}
constexpr std::uint64_t texture_hash(std::uint32_t id) {
    return id == kBeachTreeSpriteId ? kBeachTreeTextureHash :
        id == kRubberTreeSpriteId ? kRubberTreeTextureHash :
        id == kBlueberrySpriteId ? kBlueberryTextureHash :
        (supported_sprite(id) ? kTextureHash : 0U);
}

struct Sample {
    std::array<float, 3> position{};
    float scale = 0.0F;
    std::int16_t yaw = 0, pitch = 0, roll = 0;
    std::uint32_t identity = 0;
    bool valid = false;
    std::uint16_t sprite_id = kSpriteId;
    // Captured on the guest thread, never a live level-model pointer.
    std::array<float, 3> attachment{};
    bool attachment_valid = false;
};
struct Vertex {
    std::array<float, 3> position{}, normal{};
    std::array<float, 2> uv{};
};
// Keep the mesh in fixed local coordinates. Only its transform is temporal;
// near/far meshes need not have matching vertices or even the same topology.
inline constexpr float kLocalCoordinateScale = 8192.0F;
inline constexpr bool kInterpolateMeshVertices = false;
using ModelTransform = std::array<std::array<float, 4>, 4>;
ModelTransform model_transform(const Sample& sample);
std::array<std::int16_t, 3> local_position(const Vertex& vertex);
constexpr std::uint32_t model_interpolation_key(const Sample& sample) {
    // Deliberately independent of distance/LOD. The sample already includes
    // object lifetime and camera continuity, so cuts still break matching.
    return sample.identity ^ 0x504C4E54U ^ sample.sprite_id;
}
struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
};
struct Assets {
    Mesh near_mesh, far_mesh;
    Mesh canopy_near, canopy_far;
    std::filesystem::path directory;
    bool ready = false;
    Mesh blueberry_near, blueberry_far;
    std::filesystem::path blueberry_directory;
    bool blueberry_ready = false;
    Mesh rubber_tree_near, rubber_tree_far;
    std::filesystem::path rubber_tree_directory;
    bool rubber_tree_ready = false;
    Mesh beach_tree_near, beach_tree_far;
    std::filesystem::path beach_tree_directory;
    bool beach_tree_ready = false;
};

bool parse_mesh(std::span<const std::uint8_t> bytes, Mesh& output, std::string& error);
bool valid_sample(const Sample& sample);
bool find_trunk_tip(std::span<const std::array<float, 3>> bark_vertices,
                    const std::array<float, 3>& anchor, std::array<float, 3>& tip);
std::array<float, 3> world_position(const Vertex& vertex, const Sample& sample);
// Each family is optional: missing blueberries must never disable the palms.
const Mesh* select_mesh(const Assets& assets, std::uint32_t sprite, bool far);
void initialise(const std::filesystem::path& directory,
                const std::filesystem::path& blueberry_directory = {},
                const std::filesystem::path& rubber_tree_directory = {},
                const std::filesystem::path& beach_tree_directory = {});
const Assets& assets();
bool available();
bool enabled();
void set_enabled(bool enabled);
} // namespace dkr::runtime::palm
