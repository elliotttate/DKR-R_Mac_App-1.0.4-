#include "palm_model.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <mutex>

namespace dkr::runtime::palm {
namespace {
Assets g_assets;
std::once_flag g_initialised;
std::atomic<bool> g_enabled{true};
std::atomic<bool> g_ready{false};
std::uint32_t read32(const std::uint8_t* p) {
    return p[0] | (std::uint32_t(p[1]) << 8U) | (std::uint32_t(p[2]) << 16U) |
           (std::uint32_t(p[3]) << 24U);
}
bool load_mesh(const std::filesystem::path& path, Mesh& mesh, std::string& error) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size > 4U * 1024U * 1024U) {
        error = "missing or oversized mesh: " + path.string();
        return false;
    }
    std::ifstream input(path, std::ios::binary);
    const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(input), {}};
    return parse_mesh(bytes, mesh, error);
}
}

bool parse_mesh(std::span<const std::uint8_t> bytes, Mesh& output, std::string& error) {
    const auto fail = [&](const char* why) { error = why; return false; };
    if (bytes.size() < 16 || std::memcmp(bytes.data(), "DKRPM001", 8) != 0)
        return fail("invalid mesh header");
    const auto vertex_count = read32(bytes.data() + 8);
    const auto index_count = read32(bytes.data() + 12);
    if (!vertex_count || vertex_count > 60000 || !index_count ||
        index_count > 120000 || index_count % 3)
        return fail("mesh exceeds the runtime budget");
    if (bytes.size() != 16ULL + 32ULL * vertex_count + 4ULL * index_count)
        return fail("truncated mesh or trailing data");
    Mesh parsed;
    parsed.vertices.resize(vertex_count);
    parsed.indices.resize(index_count);
    std::size_t offset = 16;
    for (auto& vertex : parsed.vertices) {
        std::array<float, 8> fields{};
        for (auto& field : fields) {
            field = std::bit_cast<float>(read32(bytes.data() + offset));
            offset += 4;
            if (!std::isfinite(field)) return fail("non-finite mesh data");
        }
        for (std::size_t axis = 0; axis < 3; ++axis) {
            if (std::abs(fields[axis]) > 2 || std::abs(fields[axis + 3]) > 1.01F)
                return fail("invalid normalized position or normal");
            vertex.position[axis] = fields[axis];
            vertex.normal[axis] = fields[axis + 3];
        }
        if (std::abs(fields[6]) > 16 || std::abs(fields[7]) > 16)
            return fail("invalid texture coordinates");
        vertex.uv = {fields[6], fields[7]};
    }
    for (auto& index : parsed.indices) {
        index = read32(bytes.data() + offset);
        offset += 4;
        if (index >= vertex_count) return fail("triangle index outside mesh");
    }
    output = std::move(parsed);
    error.clear();
    return true;
}

bool valid_sample(const Sample& sample) {
    if (sample.attachment_valid && !std::all_of(sample.attachment.begin(), sample.attachment.end(),
        [](float p) { return std::isfinite(p) && std::abs(p) < 30000; })) return false;
    return sample.valid && supported_sprite(sample.sprite_id) && sample.identity != 0 && std::isfinite(sample.scale) &&
        sample.scale > 0 && sample.scale <= 32 &&
        std::all_of(sample.position.begin(), sample.position.end(), [](float p) {
            return std::isfinite(p) && std::abs(p) < 30000;
        });
}

bool find_trunk_tip(std::span<const std::array<float, 3>> bark_vertices,
                    const std::array<float, 3>& anchor, std::array<float, 3>& tip) {
    // Retail curved trunks have triangular rings. Use only a compact end ring
    // near the authored crown; never bridge to distant bark or terrain.
    std::vector<std::array<float, 3>> nearby;
    const auto distance2 = [](const auto& a, const auto& b) {
        float d = 0; for (unsigned i = 0; i < 3; ++i) d += (a[i]-b[i])*(a[i]-b[i]);
        return d;
    };
    for (const auto& v : bark_vertices) {
        if (distance2(v, anchor) <= 10000.F &&
            std::find(nearby.begin(), nearby.end(), v) == nearby.end()) nearby.push_back(v);
    }
    if (nearby.size() < 3) return false;
    std::sort(nearby.begin(), nearby.end(), [&](const auto& a, const auto& b) {
        return distance2(a, anchor) < distance2(b, anchor);
    });
    for (unsigned i = 0; i < 3; ++i)
        for (unsigned j = i + 1; j < 3; ++j)
            if (distance2(nearby[i], nearby[j]) > 1600.F) return false;
    for (unsigned axis = 0; axis < 3; ++axis)
        tip[axis] = (nearby[0][axis] + nearby[1][axis] + nearby[2][axis]) / 3.F;
    return true;
}

std::array<float, 3> world_position(const Vertex& vertex, const Sample& sample) {
    // Verified from the decoded tile offsets: PalmPlant occupies y=2..68.
    // Canopies attach at the trunk-cut plane, not the source GLB's center.
    constexpr float radians = 6.283185307179586F / 65536.0F;
    float x = vertex.position[0] * 66.0F;
    float y = vertex.position[1] * 66.0F + 2.0F;
    float z = vertex.position[2] * 66.0F;
    if (balloon_sprite(sample.sprite_id)) {
        // USA 1.0/1.1 decoded sprite quads: blue y=4..70, other weapon
        // colors 6..72, gold 22..70, silver 21..71. Mesh X/Z width is 1.
        const bool gold = sample.sprite_id == 154, silver = sample.sprite_id == 155;
        const float height = gold ? 48.F : silver ? 50.F : 66.F;
        const float bottom = gold ? 22.F : silver ? 21.F : sample.sprite_id == 147 ? 4.F : 6.F;
        x = vertex.position[0] * 43.F;
        y = vertex.position[1] * height + bottom;
        z = vertex.position[2] * 43.F;
    } else if (sample.sprite_id == kBananaSpriteId) {
        // USA 1.0/1.1 sprite 156: anchor (36,66), tile y=6, height=51.
        // The quad spans y=9..59; keep that authored floating offset.
        x = vertex.position[0] * 50.0F;
        y = vertex.position[1] * 50.0F + 9.0F;
        z = vertex.position[2] * 50.0F;
    } else if (sample.sprite_id == kBeachTreeSpriteId) {
        // USA 1.0/1.1 BeachTree: anchor 122; tile vertices span -6..116.
        // Preserve that authored foot offset and full 122-unit height.
        x = vertex.position[0] * 122.0F;
        y = vertex.position[1] * 122.0F - 6.0F;
        z = vertex.position[2] * 122.0F;
    } else if (sample.sprite_id == kRubberTreeSpriteId) {
        // RubberTree: highest authored tile is 115 units above the ground anchor.
        x = vertex.position[0] * 115.0F;
        y = vertex.position[1] * 115.0F;
        z = vertex.position[2] * 115.0F;
    } else if (sample.sprite_id == kBlueberrySpriteId) {
        // USA 1.0/1.1 BlueBerryBush: anchor y=122; six tiles span y=0..113.
        // The USDZ's bottom stem is its ground pivot, not its centered origin.
        x = vertex.position[0] * 113.0F;
        y = vertex.position[1] * 113.0F;
        z = vertex.position[2] * 113.0F;
    } else if (sample.sprite_id == 114) {
        x = vertex.position[0] * 139.0F;
        y = (vertex.position[1] - 0.50F) * 139.0F;
        z = vertex.position[2] * 139.0F;
    } else if (sample.sprite_id == 116) {
        x = vertex.position[0] * 78.0F;
        y = (vertex.position[1] - 0.50F) * 60.0F;
        z = vertex.position[2] * 78.0F;
    }
    const bool attached = sample.attachment_valid &&
                          (sample.sprite_id == 114 || sample.sprite_id == 116);
    if (attached) {
        // Measured common cut-ring center for both LODs; leave a small overlap
        // below the original cap so rounding and LOD changes cannot open a seam.
        const float width = sample.sprite_id == 114 ? 139.F : 78.F;
        x += .010F * width;
        z -= .0025F * width;
    }
    // One mesh covers all eight original frames. Use all 256 phase values so
    // each simulation update advances smoothly; RT64 interpolates the rigid
    // transform between updates, including the 255->0 phase wrap.
    const float yaw = (sample.yaw + (sample.sprite_id == kBananaSpriteId ?
        static_cast<int>(sample.animation_phase) * 256 : 0)) * radians;
    const float cy = std::cos(yaw), sy = std::sin(yaw);
    const float cp = std::cos(sample.pitch * radians), sp = std::sin(sample.pitch * radians);
    const float cr = std::cos(sample.roll * radians), sr = std::sin(sample.roll * radians);
    const float pitched_y = y * cp - z * sp;
    z = y * sp + z * cp;
    y = pitched_y;
    const float rolled_x = x * cr - y * sr;
    y = x * sr + y * cr;
    x = rolled_x;
    const auto& origin = attached ? sample.attachment : sample.position;
    return {origin[0] + sample.scale * (x * cy + z * sy),
            origin[1] + sample.scale * y - (attached ? 3.F : 0.F),
            origin[2] + sample.scale * (-x * sy + z * cy)};
}

ModelTransform model_transform(const Sample& sample) {
    ModelTransform result{};
    const auto origin = world_position(Vertex{}, sample);
    auto local_sample = sample;
    local_sample.position = {};
    local_sample.attachment = {};
    const auto local_origin = world_position(Vertex{}, local_sample);
    for (std::size_t axis = 0; axis < 3; ++axis) {
        Vertex unit{};
        unit.position[axis] = 1.0F;
        const auto endpoint = world_position(unit, local_sample);
        for (std::size_t component = 0; component < 3; ++component)
            result[axis][component] =
                (endpoint[component] - local_origin[component]) / kLocalCoordinateScale;
    }
    result[3] = {origin[0], origin[1], origin[2], 1.0F};
    return result;
}

std::array<std::int16_t, 3> local_position(const Vertex& vertex) {
    std::array<std::int16_t, 3> result{};
    // parse_mesh bounds all coordinates to [-2, 2], well within int16 here.
    for (std::size_t axis = 0; axis < 3; ++axis)
        result[axis] = static_cast<std::int16_t>(
            std::lround(vertex.position[axis] * kLocalCoordinateScale));
    return result;
}

const Mesh* select_mesh(const Assets& loaded, std::uint32_t sprite, bool far) {
    if (balloon_sprite(sprite)) {
        if (!loaded.balloon_ready) return nullptr;
        if (collectible_balloon(sprite)) return far ? &loaded.collectible_far : &loaded.collectible_near;
        return far ? &loaded.balloon_far : &loaded.balloon_near;
    }
    if (sprite == kBananaSpriteId)
        return loaded.banana_ready ? (far ? &loaded.banana_far : &loaded.banana_near) : nullptr;
    if (sprite == kBeachTreeSpriteId)
        return loaded.beach_tree_ready ? (far ? &loaded.beach_tree_far : &loaded.beach_tree_near) : nullptr;
    if (sprite == kRubberTreeSpriteId)
        return loaded.rubber_tree_ready ? (far ? &loaded.rubber_tree_far : &loaded.rubber_tree_near) : nullptr;
    if (sprite == kBlueberrySpriteId)
        return loaded.blueberry_ready ? (far ? &loaded.blueberry_far : &loaded.blueberry_near) : nullptr;
    if (!loaded.ready || !supported_sprite(sprite)) return nullptr;
    if (sprite == kSpriteId) return far ? &loaded.far_mesh : &loaded.near_mesh;
    return far ? &loaded.canopy_far : &loaded.canopy_near;
}

void initialise(const std::filesystem::path& directory,
                const std::filesystem::path& blueberry_directory,
                const std::filesystem::path& rubber_tree_directory,
                const std::filesystem::path& beach_tree_directory,
                const std::filesystem::path& banana_directory,
                const std::filesystem::path& balloon_directory) {
    std::call_once(g_initialised, [&] {
        const char* opt_out = std::getenv("DKR_PALM_3D");
        g_enabled.store(!opt_out || std::strcmp(opt_out, "0") != 0);
        std::string error;
        Assets loaded;
        if (!directory.empty() &&
            load_mesh(directory / "near.dkrmesh", loaded.near_mesh, error) &&
            load_mesh(directory / "far.dkrmesh", loaded.far_mesh, error) &&
            load_mesh(directory / "canopy-near.dkrmesh", loaded.canopy_near, error) &&
            load_mesh(directory / "canopy-far.dkrmesh", loaded.canopy_far, error)) {
            loaded.directory = directory;
            loaded.ready = true;
            std::fprintf(stderr, "[palm3d] ready: %zu / %zu triangles; full tree 115, canopies 114/116\n",
                         loaded.near_mesh.indices.size() / 3, loaded.far_mesh.indices.size() / 3);
        } else if (!directory.empty()) {
            std::fprintf(stderr, "[palm3d] sprite fallback: %s\n", error.c_str());
        }
        if (!blueberry_directory.empty() &&
            load_mesh(blueberry_directory / "near.dkrmesh", loaded.blueberry_near, error) &&
            load_mesh(blueberry_directory / "far.dkrmesh", loaded.blueberry_far, error)) {
            loaded.blueberry_directory = blueberry_directory;
            loaded.blueberry_ready = true;
            std::fprintf(stderr, "[plant3d] blueberry ready: %zu / %zu triangles; sprite 108\n",
                         loaded.blueberry_near.indices.size() / 3, loaded.blueberry_far.indices.size() / 3);
        } else if (!blueberry_directory.empty()) {
            std::fprintf(stderr, "[plant3d] blueberry sprite fallback: %s\n", error.c_str());
        }
        if (!rubber_tree_directory.empty() &&
            load_mesh(rubber_tree_directory / "near.dkrmesh", loaded.rubber_tree_near, error) &&
            load_mesh(rubber_tree_directory / "far.dkrmesh", loaded.rubber_tree_far, error)) {
            loaded.rubber_tree_directory = rubber_tree_directory;
            loaded.rubber_tree_ready = true;
            std::fprintf(stderr, "[plant3d] rubber tree ready: %zu / %zu triangles; sprite 113\n",
                         loaded.rubber_tree_near.indices.size() / 3, loaded.rubber_tree_far.indices.size() / 3);
        } else if (!rubber_tree_directory.empty()) {
            std::fprintf(stderr, "[plant3d] rubber tree sprite fallback: %s\n", error.c_str());
        }
        if (!beach_tree_directory.empty() &&
            load_mesh(beach_tree_directory / "near.dkrmesh", loaded.beach_tree_near, error) &&
            load_mesh(beach_tree_directory / "far.dkrmesh", loaded.beach_tree_far, error)) {
            loaded.beach_tree_directory = beach_tree_directory;
            loaded.beach_tree_ready = true;
            std::fprintf(stderr, "[plant3d] beach tree ready: %zu / %zu triangles; sprite 107\n",
                         loaded.beach_tree_near.indices.size() / 3, loaded.beach_tree_far.indices.size() / 3);
        } else if (!beach_tree_directory.empty()) {
            std::fprintf(stderr, "[plant3d] beach tree sprite fallback: %s\n", error.c_str());
        }
        if (!banana_directory.empty() &&
            load_mesh(banana_directory / "near.dkrmesh", loaded.banana_near, error) &&
            load_mesh(banana_directory / "far.dkrmesh", loaded.banana_far, error)) {
            loaded.banana_directory = banana_directory;
            loaded.banana_ready = true;
            std::fprintf(stderr, "[item3d] banana ready: %zu / %zu triangles; sprite 156, guest-phase rotation\n",
                         loaded.banana_near.indices.size() / 3, loaded.banana_far.indices.size() / 3);
        } else if (!banana_directory.empty()) {
            std::fprintf(stderr, "[item3d] banana sprite fallback: %s\n", error.c_str());
        }
        if (!balloon_directory.empty() &&
            load_mesh(balloon_directory / "near.dkrmesh", loaded.balloon_near, error) &&
            load_mesh(balloon_directory / "far.dkrmesh", loaded.balloon_far, error) &&
            load_mesh(balloon_directory / "collectible-near.dkrmesh", loaded.collectible_near, error) &&
            load_mesh(balloon_directory / "collectible-far.dkrmesh", loaded.collectible_far, error)) {
            loaded.balloon_directory = balloon_directory;
            loaded.balloon_ready = true;
            std::fprintf(stderr, "[item3d] balloons ready: %zu weapon / %zu collectible triangles; sprites 147-151,154,155\n",
                         loaded.balloon_near.indices.size()/3, loaded.collectible_near.indices.size()/3);
        } else if (!balloon_directory.empty()) {
            std::fprintf(stderr, "[item3d] balloon sprite fallback: %s\n", error.c_str());
        }
        const bool any_ready = loaded.ready || loaded.blueberry_ready || loaded.rubber_tree_ready ||
                               loaded.beach_tree_ready || loaded.banana_ready || loaded.balloon_ready;
        g_assets = std::move(loaded);
        g_ready.store(any_ready, std::memory_order_release);
    });
}
const Assets& assets() { return g_assets; }
bool available() { return g_ready.load(std::memory_order_acquire); }
bool enabled() { return available() && g_enabled.load(std::memory_order_relaxed); }
void set_enabled(bool value) { g_enabled.store(value, std::memory_order_relaxed); }
} // namespace dkr::runtime::palm
