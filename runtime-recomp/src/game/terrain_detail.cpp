#include "terrain_detail.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <tuple>

namespace dkr::runtime::terrain {
namespace {
std::mutex settings_mutex, stats_mutex;
Settings current_settings;
Statistics current_stats;
Vec3 add(Vec3 a, Vec3 b) {
    for (int i = 0; i < 3; ++i)
        a[i] += b[i];
    return a;
}
Vec3 sub(Vec3 a, Vec3 b) {
    for (int i = 0; i < 3; ++i)
        a[i] -= b[i];
    return a;
}
Vec3 mul(Vec3 a, float b) {
    for (float& v : a)
        v *= b;
    return a;
}
float dot(Vec3 a, Vec3 b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
Vec3 cross(Vec3 a, Vec3 b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
Vec3 normal(Vec3 a) {
    float l = std::sqrt(dot(a, a));
    return l > 1e-6F ? mul(a, 1 / l) : Vec3{};
}
float smooth(float a) {
    a = std::clamp(a, 0.F, 1.F);
    return a * a * (3 - 2 * a);
}
bool ground(Material m) {
    return m == Material::Grass || m == Material::Sand || m == Material::Snow || m == Material::Earth ||
           m == Material::Ice || m == Material::Paving;
}
bool selected(Material m) {
    return ground(m) || m == Material::Rock;
}
using Point = std::array<int, 3>;
Point point(Vec3 p) {
    return {int(std::lround(p[0])), int(std::lround(p[1])), int(std::lround(p[2]))};
}
using Edge = std::pair<Point, Point>;
Edge edge(Vec3 a, Vec3 b) {
    auto x = point(a), y = point(b);
    return x < y ? Edge{x, y} : Edge{y, x};
}
struct Node {
    Vec3 direction{};
    bool protected_point = false, ground = false, sloped_ground = false;
};
struct EdgeInfo {
    unsigned selected_count = 0, total = 0;
    bool grass = false, rock = false;
    float rock_centre_y = 0;
};
std::uint32_t hash(int x, int y, int z) {
    std::uint32_t h =
        std::uint32_t(x) * 0x8da6b343U ^ std::uint32_t(y) * 0xd8163841U ^ std::uint32_t(z) * 0xcb1ab31fU ^ 0x44b35a21U;
    h ^= h >> 16;
    h *= 0x7feb352dU;
    h ^= h >> 15;
    h *= 0x846ca68bU;
    return h ^ (h >> 16);
}
struct Reader {
    std::span<const std::uint8_t> bytes;
    bool valid(std::uint32_t a, std::uint32_t n) const {
        return a >= 0x80000000U && a <= 0x807fffffU && std::uint64_t(a & 0x7fffffU) + n <= bytes.size();
    }
    std::uint8_t u8(std::uint32_t a) const {
        return bytes[(a & 0x7fffffU) ^ 3U];
    }
    std::uint16_t u16(std::uint32_t a) const {
        return (std::uint16_t(u8(a)) << 8) | u8(a + 1);
    }
    std::int16_t s16(std::uint32_t a) const {
        return static_cast<std::int16_t>(u16(a));
    }
    std::uint32_t u32(std::uint32_t a) const {
        return (std::uint32_t(u16(a)) << 16) | u16(a + 2);
    }
};
} // namespace

Settings settings() {
    std::lock_guard lock(settings_mutex);
    return current_settings;
}
void set_settings(Settings s) {
    s.mode = static_cast<Mode>(std::clamp(int(s.mode), 0, 4));
    s.strength = std::clamp(s.strength, 0, 150);
    s.quality = std::clamp(s.quality, 0, 2);
    std::lock_guard lock(settings_mutex);
    current_settings = s;
}
const char* mode_name(Mode m) {
    switch (m) {
    case Mode::Original:
        return "Original";
    case Mode::Surface:
        return "Surface detail";
    case Mode::Geometry:
        return "Surface + geometry";
    case Mode::Materials:
        return "Material debug";
    case Mode::Boundaries:
        return "Boundary debug";
    }
    return "Original";
}
Statistics statistics() {
    std::lock_guard lock(stats_mutex);
    return current_stats;
}
void publish_draw_statistics(std::uint32_t p, std::uint32_t t, double ms) {
    std::lock_guard lock(stats_mutex);
    current_stats.drawn_patches = p;
    current_stats.drawn_triangles = t;
    current_stats.submit_ms = ms;
}
float noise(Vec3 p) {
    int x = int(std::floor(p[0])), y = int(std::floor(p[1])), z = int(std::floor(p[2]));
    float u = smooth(p[0] - x), v = smooth(p[1] - y), w = smooth(p[2] - z), out = 0;
    for (int a = 0; a < 2; ++a)
        for (int b = 0; b < 2; ++b)
            for (int c = 0; c < 2; ++c)
                out += (float(hash(x + a, y + b, z + c) & 0xffffffU) / 8388607.5F - 1.F) * (a ? u : 1 - u) *
                       (b ? v : 1 - v) * (c ? w : 1 - w);
    return out;
}
Material classify(std::uint32_t id, std::uint8_t surface, std::uint32_t flags, Vec3 n, bool opaque, bool animated,
                  std::uint32_t level_id) {
    if ((flags & 0x2000U) || surface == 11 || surface == 14 || surface == 15)
        return Material::Water;
    if (!opaque || animated || (flags & (0x4U | 0x10U | 0x100U | 0x800U | 0x8000U | 0x10000U | 0x40000U | 0x8000000U)))
        return Material::Protected;
    if (surface == 3 || surface == 12 || id == 1074 || id == 1075 || id == 967)
        return Material::Path;
    if (surface == 1 && std::abs(n[1]) > .55F)
        return Material::Grass;
    // ROM-backed, visually audited material families. Authored surface types
    // cover natural ground globally; explicit identities cover DEFAULT/STONE
    // ground in the same courses, hubs, battle arenas and boss tracks.
    const auto listed = [id](std::initializer_list<std::uint32_t> ids) {
        return std::find(ids.begin(), ids.end(), id) != ids.end();
    };
    if (std::abs(n[1]) > .55F) {
        if (listed({181, 215, 235, 1045, 1254}))
            return Material::Grass;
        if (surface == 2 || listed({26, 142, 153, 226, 236, 265, 271, 809, 1046}))
            return Material::Sand;
        if (surface == 13 || listed({78, 173, 184, 267, 287, 1002}))
            return Material::Snow;
        if (surface == 10)
            return Material::Ice;
        if (listed({149, 795, 814, 956}))
            return Material::Paving;
        // Texture 96 is also used on the Wizpig statue. Its natural-earth
        // interpretation is restricted to the forest levels that use it as soil.
        const bool forest_soil = level_id == 2 || level_id == 18 || level_id == 19 || level_id == 31 || level_id == 50;
        if (listed({20, 30, 44, 180, 268, 554, 884, 1397}) || (id == 96 && forest_soil))
            return Material::Earth;
    }
    // Rock texture allowlist is deliberately explicit; geometry alone cannot
    // distinguish a natural cliff from a building, bridge, or authored statue.
    // Masonry, timber, metallic panels, lava, buildings and statue textures
    // intentionally do not appear here. Traversable rock faces remain pinned.
    if (listed({21,   22,   23,   28,   29,   30,   44,   49,   55,   68,   69,   111,  114,  125,
                145,  168,  172,  189,  191,  192,  196,  208,  218,  219,  224,  225,  238,  240,
                251,  268,  277,  284,  286,  288,  306,  307,  308,  697,  700,  716,  717,  846,
                890,  891,  892,  907,  908,  909,  910,  960,  996,  997,  998,  1003, 1037, 1039,
                1079, 1081, 1105, 1110, 1111, 1237, 1239, 1240, 1246, 1257, 1258, 1273, 1274, 1275}))
        return Material::Rock;
    return Material::Protected;
}
std::array<std::uint8_t, 4> debug_color(Material m) {
    switch (m) {
    case Material::Grass:
        return {40, 255, 70, 255};
    case Material::Rock:
        return {255, 120, 35, 255};
    case Material::Water:
        return {30, 140, 255, 255};
    case Material::Path:
        return {255, 230, 35, 255};
    case Material::Sand:
        return {235, 195, 130, 255};
    case Material::Snow:
        return {200, 245, 255, 255};
    case Material::Earth:
        return {150, 90, 45, 255};
    case Material::Ice:
        return {40, 230, 235, 255};
    case Material::Paving:
        return {150, 160, 180, 255};
    default:
        return {130, 75, 165, 255};
    }
}

bool decode_level(std::span<const std::uint8_t> rdram, std::uint32_t model, std::uint32_t tc, std::uint32_t ntc,
                  std::vector<SourceTriangle>& output, std::string& error, std::uint32_t level_id) {
    Reader r{rdram};
    const auto fail = [&](const char* s) {
        error = s;
        return false;
    };
    if (!r.valid(model, 0x4c))
        return fail("invalid level model");
    auto textures = r.u32(model), segments = r.u32(model + 4);
    unsigned nt = r.u16(model + 0x18), ns = r.u16(model + 0x1a);
    if (nt > 255 || ns == 0 || ns > 512 || !r.valid(textures, nt * 8) || !r.valid(segments, ns * 0x44))
        return fail("invalid level tables");
    std::unordered_map<std::uint32_t, std::uint32_t> ids;
    if (ntc <= 700 && r.valid(tc, ntc * 8))
        for (unsigned i = 0; i < ntc; ++i)
            ids[r.u32(tc + i * 8 + 4)] = r.u32(tc + i * 8) & 0x7fffU;
    std::vector<SourceTriangle> result;
    for (unsigned si = 0; si < ns; ++si) {
        auto seg = segments + si * 0x44, v = r.u32(seg), f = r.u32(seg + 4), b = r.u32(seg + 12);
        unsigned nv = r.u16(seg + 0x1c), nf = r.u16(seg + 0x1e), nb = r.u16(seg + 0x20), opaque = r.u8(seg + 0x40);
        if (nv > 8192 || nf > 8192 || nb > 4096 || opaque > nb || !r.valid(v, nv * 10) || !r.valid(f, nf * 16) ||
            !r.valid(b, (nb + 1) * 12))
            return fail("invalid segment");
        for (unsigned bi = 0; bi < nb; ++bi) {
            auto batch = b + bi * 12;
            unsigned ti = r.u8(batch), vb = r.u16(batch + 2), ve = r.u16(batch + 14), fb = r.u16(batch + 4),
                     fe = r.u16(batch + 16);
            if (vb > ve || ve > nv || fb > fe || fe > nf)
                return fail("invalid batch ranges");
            auto flags = r.u32(batch + 8);
            std::uint32_t id = 0xffffffffU;
            std::uint8_t surface = 255;
            bool animated = false;
            if (ti < nt) {
                auto texture = r.u32(textures + ti * 8);
                surface = r.u8(textures + ti * 8 + 7);
                auto found = ids.find(texture);
                if (found != ids.end())
                    id = found->second;
                if (!r.valid(texture, 32))
                    return fail("invalid texture");
                animated = (r.u16(texture + 0x12) >> 8) > 1;
                flags |= r.u16(texture + 6) & (0x4U | 0x10U);
            }
            for (unsigned fi = fb; fi < fe; ++fi) {
                if (result.size() >= 100000)
                    return fail("terrain source exceeds budget");
                SourceTriangle t;
                t.address = (f + fi * 16) & 0x7fffffU;
                t.segment = si;
                t.texture_id = id;
                t.surface = surface;
                t.flags = flags;
                t.triangle_flags = r.u8(f + fi * 16);
                for (unsigned c = 0; c < 3; ++c) {
                    unsigned vi = vb + r.u8(f + fi * 16 + 1 + c);
                    if (vi >= ve)
                        return fail("invalid triangle index");
                    auto a = v + vi * 10;
                    auto& dest = t.vertices[c];
                    dest.position = {float(r.s16(a)), float(r.s16(a + 2)), float(r.s16(a + 4))};
                    for (unsigned k = 0; k < 4; ++k)
                        dest.color[k] = r.u8(a + 6 + k);
                    dest.uv = {float(r.s16(f + fi * 16 + 4 + c * 4)), float(r.s16(f + fi * 16 + 6 + c * 4))};
                }
                auto n = normal(cross(sub(t.vertices[1].position, t.vertices[0].position),
                                      sub(t.vertices[2].position, t.vertices[0].position)));
                t.material = classify(id, surface, flags, n, bi < opaque, animated, level_id);
                result.push_back(t);
            }
        }
    }
    output = std::move(result);
    return true;
}

bool Cache::build(std::span<const SourceTriangle> source, Settings config, std::string& error) {
    const auto start = std::chrono::steady_clock::now();
    std::map<Point, Node> nodes;
    std::map<Edge, EdgeInfo> edges;
    if (source.size() > 100000) {
        error = "terrain source exceeds budget";
        return false;
    }
    for (const auto& t : source) {
        auto face = normal(cross(sub(t.vertices[1].position, t.vertices[0].position),
                                 sub(t.vertices[2].position, t.vertices[0].position)));
        // Shared geometry gets one direction even where texture UVs split it.
        for (int c = 0; c < 3; ++c) {
            auto& node = nodes[point(t.vertices[c].position)];
            if (!selected(t.material))
                node.protected_point = true;
            else if (t.material == Material::Rock) {
                node.direction = add(node.direction, face);
                // Rock materials can also be used on drivable shelves. Keep
                // these faces and their shared cliff corners on the source mesh.
                if (std::abs(face[1]) > .55F)
                    node.protected_point = true;
            } else if (ground(t.material)) {
                node.ground = true;
                if (t.material == Material::Ice || t.material == Material::Paving)
                    node.protected_point = true;
                if (std::abs(face[1]) < .995F)
                    node.sloped_ground = true;
            }
            auto& e = edges[edge(t.vertices[c].position, t.vertices[(c + 1) % 3].position)];
            ++e.total;
            if (selected(t.material))
                ++e.selected_count;
            if (t.material == Material::Grass)
                e.grass = true;
            if (t.material == Material::Rock) {
                e.rock = true;
                e.rock_centre_y =
                    (t.vertices[0].position[1] + t.vertices[1].position[1] + t.vertices[2].position[1]) / 3;
            }
        }
    }
    std::set<Edge> boundaries;
    for (const auto& [e, info] : edges)
        if (info.selected_count && (info.total != 2 || info.selected_count != 2)) {
            boundaries.insert(e);
            nodes[e.first].protected_point = true;
            nodes[e.second].protected_point = true;
        }
    for (auto& [p, n] : nodes) {
        n.direction = n.protected_point || n.sloped_ground ? Vec3{} : normal(n.direction);
        // Natural ground and its cliff share horizontal displacement. Ground
        // height stays authored; sloped joins are pinned in all three axes.
        if (n.ground)
            n.direction[1] = 0;
    }
    std::unordered_map<std::uint32_t, Patch> patches;
    std::unordered_map<std::uint32_t, SourceTriangle> originals;
    Statistics stats;
    stats.source_triangles = source.size();
    stats.protected_edges = boundaries.size();
    // Uniform subdivision keeps every selected edge conforming across segments.
    const int divisions = config.quality == 0 ? 4 : config.quality == 1 ? 8 : 16;
    for (const auto& t : source) {
        originals.emplace(t.address, t);
        const bool active = selected(t.material);
        if (!active && config.mode != Mode::Materials)
            continue;
        if (active)
            ++stats.selected_triangles;
        const int n = active ? divisions : 1;
        if (stats.detail_triangles + unsigned(n * n) > 1500000) {
            error = "terrain detail exceeds budget";
            return false;
        }
        Patch patch;
        patch.material = t.material;
        patch.segment = t.segment;
        auto face = normal(cross(sub(t.vertices[1].position, t.vertices[0].position),
                                 sub(t.vertices[2].position, t.vertices[0].position)));
        std::array<Vec3, 3> directions;
        std::array<bool, 3> border;
        for (int c = 0; c < 3; ++c) {
            directions[c] = nodes[point(t.vertices[c].position)].direction;
            border[c] = boundaries.contains(edge(t.vertices[(c + 1) % 3].position, t.vertices[(c + 2) % 3].position));
        }
        std::vector<std::vector<unsigned>> index(n + 1);
        for (int i = 0; i <= n; ++i)
            for (int j = 0; j <= n - i; ++j) {
                std::array<float, 3> w{float(n - i - j) / n, float(i) / n, float(j) / n};
                DetailVertex v;
                Vec3 dir{};
                std::array<float, 4> base_color{};
                for (int c = 0; c < 3; ++c) {
                    v.base = add(v.base, mul(t.vertices[c].position, w[c]));
                    dir = add(dir, mul(directions[c], w[c]));
                    for (int k = 0; k < 2; ++k)
                        v.uv[k] += t.vertices[c].uv[k] * w[c];
                    for (int k = 0; k < 4; ++k)
                        base_color[k] += t.vertices[c].color[k] * w[c];
                }
                float mask = 1.F;
                for (int c = 0; c < 3; ++c)
                    if (border[c])
                        mask = std::min(mask, smooth(w[c] * 8.F));
                // An adjoining protected edge may affect only one of the two
                // triangles. Evaluate shared edges canonically so that its fade
                // cannot pull their coincident samples apart.
                for (int c = 0; c < 3; ++c)
                    if (w[c] == 0.F)
                        mask = border[c] ? 0.F : 1.F;
                v.boundary_weight = mask;
                // Blend the shared edge direction into the face inside the patch.
                float interior = 27.F * w[0] * w[1] * w[2];
                if (t.material == Material::Rock && std::abs(face[1]) > .55F)
                    dir = {};
                else if (t.material == Material::Rock)
                    dir = add(mul(dir, 1 - interior), mul(face, interior));
                else if (t.material == Material::Ice || t.material == Material::Paving)
                    dir = {};
                else
                    dir = mul(dir, 1 - interior);
                const float strength = config.strength / 100.F;
                auto height = [&](Vec3 p) {
                    float broad = noise(mul(p, .006F));
                    float strata = std::sin(p[1] * .045F + noise(mul(p, .014F)) * 2.2F);
                    return (broad * 12.F + strata * 2.3F + noise(mul(p, .027F)) * 2.F) * strength;
                };
                v.position = add(v.base, mul(dir, mask * height(v.base)));
                float macro = noise(mul(v.base, .0045F)), detail = noise(mul(v.base, .038F));
                float shade = 1.F;
                if (t.material == Material::Grass)
                    shade = 1.F + strength * (.16F * macro + .055F * detail);
                if (t.material == Material::Sand) {
                    const float ripples = std::sin(v.base[0] * .035F + v.base[2] * .018F + macro * 2.F);
                    shade = 1.F + strength * (.13F * macro + .035F * detail + .025F * ripples);
                }
                if (t.material == Material::Snow)
                    shade = 1.F + strength * (.085F * macro + .025F * detail);
                if (t.material == Material::Earth)
                    shade = 1.F + strength * (.15F * macro + .065F * detail);
                if (t.material == Material::Ice)
                    shade = 1.F + strength * (.04F * macro + .012F * detail);
                if (t.material == Material::Paving)
                    shade = 1.F + strength * (.055F * macro + .02F * detail);
                if (t.material == Material::Rock) {
                    Vec3 gradient{};
                    for (int a = 0; a < 3; ++a) {
                        Vec3 lo = v.base, hi = v.base;
                        lo[a] -= 2;
                        hi[a] += 2;
                        gradient[a] = (height(hi) - height(lo)) * .25F;
                    }
                    auto tangent = sub(gradient, mul(face, dot(face, gradient)));
                    auto bumped = normal(sub(face, mul(tangent, mask)));
                    Vec3 light = normal({-.45F, .85F, .28F});
                    shade = 1.F + .65F * (dot(bumped, light) - dot(face, light)) + .055F * macro * strength;
                    shade -= .065F * strength * (1 - mask);
                }
                for (int k = 0; k < 3; ++k) {
                    float tint = t.material == Material::Grass ? (k == 0   ? macro * .025F * strength
                                                                  : k == 2 ? -macro * .02F * strength
                                                                           : 0)
                                                               : 0;
                    v.color[k] = std::uint8_t(std::lround(std::clamp(base_color[k] * (shade + tint), 0.F, 255.F)));
                }
                v.color[3] = std::uint8_t(std::lround(std::clamp(base_color[3], 0.F, 255.F)));
                if (config.mode == Mode::Materials)
                    v.color = debug_color(t.material);
                if (config.mode == Mode::Boundaries)
                    v.color = {std::uint8_t(255 * (1 - mask)), std::uint8_t(255 * mask), 40, 255};
                index[i].push_back(patch.vertices.size());
                patch.vertices.push_back(v);
            }
        for (int i = 0; i < n; ++i)
            for (int j = 0; j < n - i; ++j) {
                patch.indices.insert(patch.indices.end(), {index[i][j], index[i + 1][j], index[i][j + 1]});
                if (j < n - i - 1)
                    patch.indices.insert(patch.indices.end(), {index[i + 1][j], index[i + 1][j + 1], index[i][j + 1]});
            }
        if (config.vegetation && config.strength > 0 && t.material == Material::Grass) {
            for (int c = 0; c < 3; ++c) {
                auto p = t.vertices[c].position, q = t.vertices[(c + 1) % 3].position;
                const auto& info = edges.at(edge(p, q));
                if (!info.grass || !info.rock || info.total != 2)
                    continue;
                Vec3 centre =
                    mul(add(add(t.vertices[0].position, t.vertices[1].position), t.vertices[2].position), 1.F / 3);
                const float length = std::sqrt(dot(sub(p, q), sub(p, q)));
                const unsigned count = std::min(24U, unsigned(length / 70.F));
                const bool rim = info.rock_centre_y < (p[1] + q[1]) * .5F - 12.F;
                const bool base = info.rock_centre_y > (p[1] + q[1]) * .5F + 12.F;
                if (!rim && !base)
                    continue;
                for (unsigned k = 0; k < count; ++k) {
                    auto random = hash(int(p[0]) + int(k) * 37, int(p[1]), int(p[2]));
                    if (random % 3 == 0)
                        continue;
                    float f = (k + .2F + .6F * float(random & 255) / 255.F) / count;
                    Vec3 anchor = add(mul(p, 1 - f), mul(q, f));
                    const auto inward = sub(centre, anchor);
                    const float inset = std::min(rim ? 9.F : 16.F, std::sqrt(dot(inward, inward)) * .5F);
                    anchor = add(anchor, mul(normal(inward), inset));
                    const auto emit = [&](Vec3 position, std::array<std::uint8_t, 4> color) {
                        DetailVertex v;
                        v.base = anchor;
                        v.position = position;
                        v.color = color;
                        patch.decoration_vertices.push_back(v);
                    };
                    if (rim) {
                        for (unsigned blade = 0; blade < 5; ++blade) {
                            float angle = (random % 1000) * .00628F + blade * 2.4F;
                            float h = 5.F + float((random >> (blade * 3)) & 7);
                            Vec3 axis{std::cos(angle), 0, std::sin(angle)};
                            Vec3 root = add(anchor, mul(axis, float(blade % 3) * 1.8F));
                            Vec3 tip = add(add(root, mul(axis, 2.F)), {0, h, 0});
                            unsigned v = patch.decoration_vertices.size();
                            emit(add(root, mul(axis, -1.2F)), {48, 113, 19, 255});
                            emit(add(root, mul(axis, 1.2F)), {69, 143, 23, 255});
                            emit(tip, {111, 178, 39, 255});
                            patch.decoration_indices.insert(patch.decoration_indices.end(), {v, v + 1, v + 2});
                        }
                    } else if (random % 5 == 0) {
                        float radius = 3.F + float(random & 3);
                        unsigned v = patch.decoration_vertices.size();
                        for (unsigned side = 0; side < 5; ++side) {
                            float angle = side * 1.256637F;
                            emit(add(anchor, {std::cos(angle) * radius, 0, std::sin(angle) * radius}),
                                 {119, 104, 67, 255});
                        }
                        emit(add(anchor, {1, radius * .75F, -.5F}), {171, 150, 103, 255});
                        for (unsigned side = 0; side < 5; ++side)
                            patch.decoration_indices.insert(patch.decoration_indices.end(),
                                                            {v + side, v + (side + 1) % 5, v + 5});
                    }
                }
            }
        }
        stats.detail_triangles += patch.indices.size() / 3;
        patches.emplace(t.address, std::move(patch));
    }
    patches_ = std::move(patches);
    sources_ = std::move(originals);
    config_ = config;
    stats.build_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    stats_ = stats;
    {
        std::lock_guard lock(stats_mutex);
        current_stats = stats;
    }
    return true;
}

bool Cache::prepare(std::span<const std::uint8_t> rdram, std::uint32_t model, std::uint32_t tc, std::uint32_t ntc,
                    std::uint32_t scene, Settings config, std::uint32_t level_id) {
    if (scene == 0 || model == 0)
        return false;
    Reader reader{rdram};
    if (!reader.valid(model, 0x4c))
        return false;
    auto segment_table = reader.u32(model + 4);
    unsigned segment_count = reader.u16(model + 0x1a);
    if (segment_count > 512 || !reader.valid(segment_table, segment_count * 0x44))
        return false;
    std::uint64_t signature = 1469598103934665603ULL;
    const auto mix = [&](std::uint32_t v) { signature = (signature ^ v) * 1099511628211ULL; };
    for (unsigned i = 0; i < 0x20; i += 4)
        mix(reader.u32(model + i));
    for (unsigned i = 0; i < segment_count; ++i) {
        auto segment = segment_table + i * 0x44;
        for (unsigned offset : {0U, 4U, 12U, 28U, 32U, 64U})
            mix(reader.u32(segment + offset));
    }
    // Surface/geometry switch shares the same topology, shading and identity.
    auto cache_config = config;
    if (cache_config.mode == Mode::Surface)
        cache_config.mode = Mode::Geometry;
    if (scene_ == scene && model_ == model && level_id_ == level_id && header_signature_ == signature &&
        config_ == cache_config && !sources_.empty())
        return true;
    std::vector<SourceTriangle> triangles;
    std::string error;
    if (!decode_level(rdram, model, tc, ntc, triangles, error, level_id) || !build(triangles, cache_config, error)) {
        if (std::getenv("DKR_TRACE_TERRAIN"))
            std::fprintf(stderr, "[terrain] fallback: %s\n", error.c_str());
        return false;
    }
    model_ = model;
    scene_ = scene;
    level_id_ = level_id;
    header_signature_ = signature;
    std::fprintf(stderr, "[terrain] scene=%u source=%u selected=%u detail=%u boundary-edges=%u build=%.2fms\n", scene,
                 stats_.source_triangles, stats_.selected_triangles, stats_.detail_triangles, stats_.protected_edges,
                 stats_.build_ms);
    if (std::getenv("DKR_TRACE_TERRAIN")) {
        std::map<std::tuple<unsigned, unsigned, unsigned, unsigned>, unsigned> materials;
        for (const auto& t : triangles)
            ++materials[{t.texture_id, t.surface, unsigned(t.material), t.flags}];
        for (const auto& [key, count] : materials)
            std::fprintf(stderr, "[terrain-material] texture=%u surface=%u class=%u flags=%08x triangles=%u\n",
                         std::get<0>(key), std::get<1>(key), std::get<2>(key), std::get<3>(key), count);
        if (const char* path = std::getenv("DKR_TERRAIN_DUMP")) {
            std::ofstream out(std::string(path) + "-" + std::to_string(scene) + ".bin", std::ios::binary);
            out.write(reinterpret_cast<const char*>(rdram.data()), rdram.size());
            std::fprintf(stderr, "[terrain-dump] model=%08x cache=%08x count=%u scene=%u level=%u\n", model, tc, ntc,
                         scene, level_id);
        }
    }
    return true;
}
const Patch* Cache::find(std::uint32_t a) const {
    auto it = patches_.find(a);
    return it == patches_.end() ? nullptr : &it->second;
}
const SourceTriangle* Cache::source(std::uint32_t a) const {
    auto it = sources_.find(a);
    return it == sources_.end() ? nullptr : &it->second;
}
} // namespace dkr::runtime::terrain
