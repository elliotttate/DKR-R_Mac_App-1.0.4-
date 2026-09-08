#include "terrain_detail.hpp"
#include "terrain_qa.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>

using namespace dkr::runtime::terrain;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            std::fprintf(stderr, "line %d: %s\n", __LINE__, #x);                                                       \
            std::abort();                                                                                              \
        }                                                                                                              \
    } while (0)
SourceTriangle triangle(std::uint32_t a, Vec3 x, Vec3 y, Vec3 z, Material m) {
    SourceTriangle t;
    t.address = a;
    t.material = m;
    t.vertices = {SourceVertex{x, {180, 190, 170, 255}, {0, 0}}, SourceVertex{y, {180, 190, 170, 255}, {4096, 0}},
                  SourceVertex{z, {180, 190, 170, 255}, {0, 4096}}};
    return t;
}
int main(int argc, char** argv) {
    // QA inputs must be bounded by simulation ticks, including menu ticks.
    qa::steps = {{1, 0x1000, 0, 0}, {40, 0, 0, 0}};
    qa::route_player = 0;
    std::uint16_t buttons = 0;
    float x = 0, y = 0;
    CHECK(qa::input(0, 100, &buttons, &x, &y) && buttons == 0x1000);
    CHECK(!qa::input(1, 100, &buttons, &x, &y));
    CHECK(qa::input(0, 101, &buttons, &x, &y) && buttons == 0);
    CHECK(qa::input(0, 140, &buttons, &x, &y) && buttons == 0);
    CHECK(!qa::input(0, 141, &buttons, &x, &y));
    CHECK(classify(20, 1, 0, {0, 1, 0}, true, false) == Material::Grass);
    CHECK(classify(20, 1, 0, {0, 0, 1}, true, false) == Material::Protected);
    CHECK(classify(20, 1, 0x2000, {0, 1, 0}, true, false) == Material::Water);
    CHECK(classify(20, 1, 0x800, {0, 1, 0}, true, false) == Material::Protected);
    CHECK(classify(20, 1, 0, {0, 1, 0}, true, true) == Material::Protected);
    CHECK(classify(20, 1, 0, {0, 1, 0}, false, false) == Material::Protected);
    CHECK(classify(20, 12, 0, {0, 1, 0}, true, false) == Material::Path);
    CHECK(classify(221, 2, 0, {0, 1, 0}, true, false, 23) == Material::Sand);
    CHECK(classify(142, 0, 0, {0, 1, 0}, true, false, 23) == Material::Sand);
    CHECK(classify(274, 13, 0, {0, 1, 0}, true, false, 28) == Material::Snow);
    CHECK(classify(173, 0, 0, {0, 1, 0}, true, false, 28) == Material::Snow);
    CHECK(classify(96, 0, 0, {0, 1, 0}, true, false, 18) == Material::Earth);
    for (auto level : {23U, 18U, 28U, 7U, 29U, 19U, 5U, 8U, 31U})
        CHECK(classify(221, 2, 0, {0, 1, 0}, true, false, level) == Material::Sand);
    for (auto level : {0U, 1U, 3U, 13U, 37U, 0xffffffffU}) {
        CHECK(classify(96, 0, 0, {0, 1, 0}, true, false, level) == Material::Protected);
        CHECK(classify(929, 0, 0, {0, 0, 1}, true, false, level) == Material::Protected);
        CHECK(classify(221, 2, 0, {0, 1, 0}, true, false, level) == Material::Sand);
        CHECK(classify(238, 4, 0, {0, 0, 1}, true, false, level) == Material::Rock);
    }
    CHECK(classify(927, 10, 0, {0, 1, 0}, true, false, 6) == Material::Ice);
    CHECK(classify(149, 0, 0, {0, 1, 0}, true, false, 15) == Material::Paving);
    CHECK(classify(149, 0, 0, {0, 0, 1}, true, false, 15) == Material::Protected);
    for (auto id : {16U, 202U, 203U, 204U, 1276U, 1290U, 1293U, 321U})
        CHECK(classify(id, 0, 0, {0, 1, 0}, true, false, 7) == Material::Protected);
    for (auto id : {890U, 891U, 892U, 1037U, 1003U, 1105U, 1079U, 251U, 125U}) {
        CHECK(classify(id, 4, 0, {0, 0, 1}, true, false, 23) == Material::Rock);
        CHECK(classify(id, 4, 0, {0, 0, 1}, false, false, 23) == Material::Protected);
        CHECK(classify(id, 4, 0, {0, 0, 1}, true, true, 23) == Material::Protected);
    }
    // Castles, timber, roof tiles and opaque water proxies are not natural rock.
    for (auto id : {174U, 105U, 805U, 811U, 1156U})
        CHECK(classify(id, 0, 0, {0, 1, 0}, true, false, 28) == Material::Protected);
    for (auto surface : {2U, 13U}) {
        CHECK(classify(20, surface, 0x4, {0, 1, 0}, true, false, 23) == Material::Protected);
        CHECK(classify(20, surface, 0, {0, 1, 0}, true, true, 23) == Material::Protected);
        CHECK(classify(20, surface, 0x2000, {0, 1, 0}, true, false, 23) == Material::Water);
    }
    for (int i = -100; i < 100; ++i) {
        Vec3 p{float(i) * .125F, float(i) * .17F, float(i) * .021F};
        CHECK(noise(p) == noise(p));
        CHECK(std::abs(noise(p)) <= 1);
        auto q = p;
        q[0] += .0001F;
        CHECK(std::abs(noise(p) - noise(q)) < .002F);
    }
    std::vector<SourceTriangle> source{triangle(16, {0, 0, 0}, {600, 0, 0}, {600, 600, 0}, Material::Rock),
                                       triangle(32, {0, 0, 0}, {600, 600, 0}, {0, 600, 0}, Material::Rock)};
    source[1].segment = 1; // Same seam crosses both segment and UV ownership.
    Cache cache;
    std::string error;
    Settings config;
    CHECK(cache.build(source, config, error));
    CHECK(cache.stats().selected_triangles == 2);
    CHECK(cache.stats().detail_triangles == 128);
    CHECK(cache.stats().protected_edges == 4);
    const auto* a = cache.find(16);
    const auto* b = cache.find(32);
    CHECK(a && b);
    bool moved = false;
    for (const auto& v : a->vertices) {
        CHECK(v.color[3] == 255);
        if (v.base[1] == 0 || v.base[0] == 600)
            CHECK(v.position == v.base);
        if (v.position != v.base)
            moved = true;
        if (v.base[0] == v.base[1]) {
            auto it =
                std::find_if(b->vertices.begin(), b->vertices.end(), [&](const auto& o) { return o.base == v.base; });
            CHECK(it != b->vertices.end());
            for (int k = 0; k < 3; ++k)
                CHECK(std::abs(it->position[k] - v.position[k]) < .0001F);
        }
    }
    CHECK(moved);
    source[1].material = Material::Protected;
    CHECK(cache.build(source, config, error));
    a = cache.find(16);
    CHECK(!cache.find(32));
    for (const auto& v : a->vertices)
        if (v.base[0] == v.base[1])
            CHECK(v.position == v.base);
    source[0].material = Material::Grass;
    CHECK(cache.build(source, config, error));
    for (const auto& v : cache.find(16)->vertices)
        CHECK(v.position == v.base);
    config.strength = 0;
    source[0].material = Material::Rock;
    CHECK(cache.build(source, config, error));
    for (const auto& v : cache.find(16)->vertices) {
        CHECK(v.position == v.base);
        CHECK(v.color[0] == 180);
    }
    source[0].material = Material::Grass;
    CHECK(cache.build(source, config, error));
    for (const auto& v : cache.find(16)->vertices) {
        CHECK(v.position == v.base);
        CHECK(v.color[0] == 180 && v.color[1] == 190 && v.color[2] == 170);
    }
    CHECK(cache.find(16)->decoration_vertices.empty());
    source[0].material = Material::Rock;
    config.mode = Mode::Materials;
    CHECK(cache.build(source, config, error));
    CHECK(cache.find(32));
    std::vector<std::uint8_t> rdram(0x800000);
    std::vector<SourceTriangle> decoded = source;
    CHECK(!decode_level(rdram, 0, 0, 0, decoded, error));
    CHECK(decoded.size() == source.size());
    CHECK(!decode_level(rdram, 0x807ffff0, 0, 0, decoded, error));
    // A ridge shared between grass and rock must have identical positions,
    // including across material and segment boundaries.
    source = {triangle(16, {0, 0, 0}, {600, 0, 0}, {600, 600, 0}, Material::Rock),
              triangle(32, {0, 0, 0}, {600, 600, 0}, {0, 600, 0}, Material::Rock),
              triangle(48, {0, 600, 0}, {600, 600, 0}, {600, 600, 600}, Material::Grass),
              triangle(64, {0, 600, 0}, {600, 600, 600}, {0, 600, 600}, Material::Grass)};
    config = {};
    CHECK(cache.build(source, config, error));
    for (auto address : {16U, 32U, 48U, 64U})
        for (const auto& v : cache.find(address)->vertices) {
            for (auto other : {16U, 32U, 48U, 64U})
                for (const auto& o : cache.find(other)->vertices)
                    if (v.base == o.base)
                        for (int k = 0; k < 3; ++k)
                            CHECK(std::abs(v.position[k] - o.position[k]) < .0001F);
            if (address >= 48)
                CHECK(v.position[1] == 600);
        }
    // A rock texture on a traversable shelf receives shading, not displacement.
    source[2].material = source[3].material = Material::Rock;
    CHECK(cache.build(source, config, error));
    for (auto address : {48U, 64U})
        for (const auto& v : cache.find(address)->vertices)
            CHECK(v.position == v.base);
    CHECK(classify(1074, 0, 0, {0, 1, 0}, true, false) == Material::Path);
    // All new natural-ground treatments share the same cliff-edge positions,
    // preserve both flat and sloped driving surfaces, and are neutral at zero.
    for (auto material : {Material::Sand, Material::Snow, Material::Earth, Material::Ice, Material::Paving}) {
        source[2].material = source[3].material = material;
        CHECK(cache.build(source, config, error));
        bool shaded = false;
        for (auto address : {48U, 64U})
            for (const auto& v : cache.find(address)->vertices) {
                CHECK(v.position[1] == v.base[1]);
                shaded |= v.color[0] != 180;
                for (auto other : {16U, 32U, 48U, 64U})
                    for (const auto& o : cache.find(other)->vertices)
                        if (v.base == o.base)
                            CHECK(v.position == o.position);
            }
        CHECK(shaded);
        config.strength = 0;
        CHECK(cache.build(source, config, error));
        for (const auto& v : cache.find(48)->vertices) {
            CHECK(v.position == v.base);
            CHECK(v.color[0] == 180 && v.color[1] == 190 && v.color[2] == 170);
        }
        config.strength = 100;
    }
    for (auto material :
         {Material::Grass, Material::Sand, Material::Snow, Material::Earth, Material::Ice, Material::Paving}) {
        auto slope = triangle(80, {0, 0, 0}, {600, 200, 0}, {600, 200, 600}, material);
        CHECK(cache.build(std::span(&slope, 1), config, error));
        for (const auto& v : cache.find(80)->vertices)
            CHECK(v.position == v.base);
    }
    if (argc >= 5 && argc <= 7) {
        std::ifstream input(argv[1], std::ios::binary);
        std::vector<std::uint8_t> snapshot((std::istreambuf_iterator<char>(input)), {});
        CHECK(snapshot.size() == 0x800000);
        CHECK(decode_level(snapshot, std::stoul(argv[2], nullptr, 16), std::stoul(argv[3], nullptr, 16),
                           std::stoul(argv[4]), decoded, error, argc == 7 ? std::stoul(argv[6]) : 0xffffffffU));
        config = {};
        if (argc >= 6)
            config.quality = std::stoi(argv[5]);
        CHECK(cache.build(decoded, config, error));
        std::map<Vec3, Vec3> positions;
        unsigned shared = 0, decorations = 0;
        for (const auto& source_triangle : decoded)
            if (const auto* patch = cache.find(source_triangle.address)) {
                decorations += patch->decoration_indices.size() / 3;
                for (const auto& v : patch->vertices) {
                    for (int axis = 0; axis < 3; ++axis)
                        CHECK(std::isfinite(v.position[axis]) && std::abs(v.position[axis]) < 32760);
                    if (patch->material == Material::Grass || patch->material == Material::Sand ||
                        patch->material == Material::Snow || patch->material == Material::Earth ||
                        patch->material == Material::Ice || patch->material == Material::Paving)
                        CHECK(std::abs(v.position[1] - v.base[1]) < .0001F);
                    auto [it, inserted] = positions.emplace(v.base, v.position);
                    if (!inserted) {
                        ++shared;
                        for (int axis = 0; axis < 3; ++axis)
                            CHECK(std::abs(v.position[axis] - it->second[axis]) < .01F);
                    }
                }
            }
        std::printf("Captured scene: %u source / %u selected / %u detail triangles; %u coincident samples; %u "
                    "decoration triangles; %.2f ms build.\n",
                    cache.stats().source_triangles, cache.stats().selected_triangles, cache.stats().detail_triangles,
                    shared, decorations, cache.stats().build_ms);
    }
    set_settings({static_cast<Mode>(900), -2, 30, true});
    auto clamped = settings();
    CHECK(int(clamped.mode) == 4 && clamped.strength == 0 && clamped.quality == 2);
    std::puts(
        "Terrain material exclusions, shared seams, protected joins, stable noise and collision-plane tests passed.");
}
