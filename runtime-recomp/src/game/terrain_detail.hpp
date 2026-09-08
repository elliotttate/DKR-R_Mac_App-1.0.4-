#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace dkr::runtime::terrain {
using Vec3 = std::array<float, 3>;
enum class Material : std::uint8_t { Protected, Grass, Rock, Water, Path, Sand, Snow, Earth, Ice, Paving };
enum class Mode : int { Original, Surface, Geometry, Materials, Boundaries };
struct Settings {
    Mode mode = Mode::Geometry;
    int strength = 100;
    int quality = 1;
    bool vegetation = true;
    bool operator==(const Settings&) const = default;
};
Settings settings();
void set_settings(Settings value);
const char* mode_name(Mode mode);

struct SourceVertex {
    Vec3 position{};
    std::array<float, 4> color{};
    std::array<float, 2> uv{};
};
struct SourceTriangle {
    std::array<SourceVertex, 3> vertices{};
    std::uint32_t address = 0, texture_id = 0, flags = 0;
    std::uint16_t segment = 0;
    std::uint8_t surface = 0, triangle_flags = 0;
    Material material = Material::Protected;
};
struct DetailVertex {
    Vec3 base{}, position{};
    std::array<float, 2> uv{};
    std::array<std::uint8_t, 4> color{};
    float boundary_weight = 0;
};
struct Patch {
    std::vector<DetailVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<DetailVertex> decoration_vertices;
    std::vector<std::uint32_t> decoration_indices;
    Material material = Material::Protected;
    std::uint16_t segment = 0;
};
struct Statistics {
    std::uint32_t source_triangles = 0, selected_triangles = 0;
    std::uint32_t detail_triangles = 0, protected_edges = 0;
    std::uint32_t drawn_triangles = 0, drawn_patches = 0;
    double build_ms = 0, submit_ms = 0;
};
Statistics statistics();
void publish_draw_statistics(std::uint32_t patches, std::uint32_t triangles, double ms);

float noise(Vec3 position);
Material classify(std::uint32_t texture_id, std::uint8_t surface, std::uint32_t flags, Vec3 normal, bool opaque,
                  bool animated, std::uint32_t level_id = 0xffffffffU);
std::array<std::uint8_t, 4> debug_color(Material material);

// All reads use the immutable graphics submission snapshot, never live RDRAM.
bool decode_level(std::span<const std::uint8_t> rdram, std::uint32_t model, std::uint32_t texture_cache,
                  std::uint32_t texture_count, std::vector<SourceTriangle>& output, std::string& error,
                  std::uint32_t level_id = 0xffffffffU);
class Cache {
  public:
    bool prepare(std::span<const std::uint8_t> rdram, std::uint32_t model, std::uint32_t texture_cache,
                 std::uint32_t texture_count, std::uint32_t scene, Settings config,
                 std::uint32_t level_id = 0xffffffffU);
    bool build(std::span<const SourceTriangle> source, Settings config, std::string& error);
    const Patch* find(std::uint32_t triangle_address) const;
    const SourceTriangle* source(std::uint32_t triangle_address) const;
    const Statistics& stats() const {
        return stats_;
    }

  private:
    std::unordered_map<std::uint32_t, Patch> patches_;
    std::unordered_map<std::uint32_t, SourceTriangle> sources_;
    std::uint32_t model_ = 0, scene_ = 0, level_id_ = 0xffffffffU;
    std::uint64_t header_signature_ = 0;
    Settings config_{};
    Statistics stats_{};
};
} // namespace dkr::runtime::terrain
