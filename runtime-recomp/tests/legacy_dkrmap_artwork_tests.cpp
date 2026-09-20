// .dkrmap artwork inside a legacy mod session.
//
// custom_tracks numbers a track's own textures after the boot texture table and
// writes those IDs into the track's level model. In a mod session that table is
// the character-augmented boot bank, and the textures have to be appended to it
// - and to every scene bank - at exactly those IDs. These checks tie the two
// sides together against the owned ROM given as the first argument.
#include "custom_tracks.hpp"
#include "legacy_mod_launch.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>

using namespace dkr::mods;
namespace tracks = dkr::runtime::custom_tracks;

namespace {
unsigned checks = 0;
void require(bool ok, const char* why) {++checks; if(!ok) throw Error(why);}
template<class F> void rejects(F fn, const char* why) {
    bool caught = false;
    try {fn();} catch(const Error&) {caught = true;}
    require(caught, why);
}

void write(const std::filesystem::path& path, const Bytes& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}
void write(const std::filesystem::path& path, const std::string& text) {
    write(path, Bytes(text.begin(), text.end()));
}
void put32(Bytes& bytes, std::size_t at, std::uint32_t value) {
    for(unsigned i = 0; i < 4; ++i) bytes[at + i] = static_cast<std::uint8_t>(value >> (24 - i * 8));
}

// One opaque RGBA16 frame, as the addon's BuildTexture writes it.
Bytes texture(std::uint8_t width, std::uint8_t height, std::uint8_t fill) {
    const std::size_t size = (32U + std::size_t(width) * height * 2U + 15U) & ~std::size_t{15U};
    Bytes bytes(size, fill);
    std::fill_n(bytes.begin(), 32, std::uint8_t{0});
    bytes[0x00] = width; bytes[0x01] = height;
    bytes[0x02] = 0x11;   // OPAQUE, RGBA16
    bytes[0x05] = 1;
    bytes[0x12] = 1;      // one frame
    bytes[0x16] = static_cast<std::uint8_t>(size >> 8);
    bytes[0x17] = static_cast<std::uint8_t>(size);
    return bytes;
}

// A level model container whose stored prefix holds the texture table, the
// shape custom_tracks rewrites (see custom_tracks_tests.cpp).
Bytes model(const std::vector<std::int32_t>& ids) {
    constexpr std::uint32_t table = 0x4C;
    const auto prefix = static_cast<std::uint32_t>(table + ids.size() * 8U);
    Bytes body(prefix, 0);
    put32(body, 0, table);
    body[0x19] = static_cast<std::uint8_t>(ids.size());
    for(std::size_t i = 0; i < ids.size(); ++i) {
        put32(body, table + i * 8U, static_cast<std::uint32_t>(ids[i]));
        body[table + i * 8U + 4] = 16; body[table + i * 8U + 5] = 16; body[table + i * 8U + 6] = 0x11;
    }
    const auto size = static_cast<std::uint32_t>(body.size() + 64U);
    Bytes out{static_cast<std::uint8_t>(size), static_cast<std::uint8_t>(size >> 8),
              static_cast<std::uint8_t>(size >> 16), static_cast<std::uint8_t>(size >> 24), 0x09, 0x00,
              static_cast<std::uint8_t>(prefix), static_cast<std::uint8_t>(prefix >> 8),
              static_cast<std::uint8_t>(~prefix), static_cast<std::uint8_t>(~prefix >> 8)};
    out.insert(out.end(), body.begin(), body.end());
    out.insert(out.end(), 64U, 'z');
    return out;
}

struct AuthoredTrack {std::string id; std::vector<Bytes> textures;};

void write_track(const std::filesystem::path& root, const AuthoredTrack& track) {
    const auto folder = root / (track.id + ".dkrmap");
    std::string adds = "{\"section\":\"LEVEL_HEADERS\",\"file\":\"h.bin\"},"
                       "{\"section\":\"LEVEL_MODELS\",\"file\":\"m.bin\"}";
    std::vector<std::int32_t> ids;
    for(std::size_t j = 0; j < track.textures.size(); ++j) {
        const auto file = "textures/" + std::to_string(j) + ".bin";
        adds += ",{\"section\":\"TEXTURES_3D\",\"file\":\"" + file + "\"}";
        write(folder / file, track.textures[j]);
        ids.push_back(tracks::kCustomTextureIdBase + static_cast<std::int32_t>(j));
    }
    write(folder / "manifest.json", "{\"schemaVersion\":1,\"id\":\"" + track.id + "\",\"name\":\"" +
                                        track.id + "\",\"adds\":[" + adds + "]}");
    // A header keeps pointing at retail object maps (73 and 5) when the track
    // ships none; 0 would be another level's objects.
    Bytes header(200, 0);
    header[0x37] = 73;
    header[0xBB] = 5;
    write(folder / "h.bin", header);
    write(folder / "m.bin", model(ids));
}

std::vector<std::int32_t> table_of(const AssetDirectory& directory, unsigned section) {
    const auto bytes = directory.read(section, 0, directory.section_size(section));
    std::vector<std::int32_t> table;
    for(std::size_t at = 0; at + 4 <= bytes.size(); at += 4) {
        table.push_back(static_cast<std::int32_t>(be32(bytes, at)));
        if(table.back() == -1) break;
    }
    return table;
}

// Every added model is served naming, in order, bank records whose bytes are
// its own track's texture files.
void require_models_resolve(const std::shared_ptr<const AssetBank>& boot,
                            const std::vector<AuthoredTrack>& authored, std::size_t base) {
    const auto retail = table_of(*AssetDirectory::build(boot), 26);
    const auto extended = tracks::build_extended_table(tracks::Section::LevelModels, retail.data());
    const auto models = retail.size() - 2;
    require(extended.size() == retail.size() + authored.size(), "Each authored track did not add one model.");
    std::size_t matched = 0;
    for(std::size_t index = models; index < models + authored.size(); ++index) {
        const auto size = extended[index + 1] - extended[index];
        const auto served = tracks::payload_for(tracks::Section::LevelModels, extended[index], size);
        require(served != nullptr, "An authored model is not served.");
        const View body(served + 10, static_cast<std::size_t>(size) - 10);
        const auto count = be16(body, 0x18);
        const auto* owner = [&]() -> const AuthoredTrack* {
            for(const auto& track : authored) if(track.textures.size() == count) return &track;
            return nullptr;
        }();
        require(owner != nullptr, "A served model names an unexpected texture count.");
        for(unsigned j = 0; j < count; ++j) {
            const auto id = be32(body, be32(body, 0) + j * 8U);
            require(id >= base && id < boot->record_count(2), "A model texture points outside the dkrmap range.");
            require(std::ranges::equal(boot->record(2, id), owner->textures[j]),
                    "A model texture ID resolves to another texture in the boot bank.");
        }
        ++matched;
    }
    require(matched == authored.size(), "Not every authored model was checked.");
}

void unit() {
    rejects([] {publish_dkrmap_artwork(nullptr);}, "Artwork was published without a boot bank.");
    rejects([] {AssetBank::append_textures(nullptr, {});}, "Artwork was appended to no bank.");
}

void corpus(const std::filesystem::path& rom) {
    const auto stock = AssetBank::stock(read_file(rom, MaxImage));
    const auto n2 = stock->record_count(2), n29 = stock->record_count(29), n32 = stock->record_count(32);
    // custom_tracks keeps its state files beside the scanned folder, so the
    // folder is nested to keep them inside this test's own directory.
    const auto sandbox = std::filesystem::temp_directory_path() / "dkr-dkrmap-artwork-tests";
    const auto root = sandbox / "custom-tracks";
    std::filesystem::remove_all(sandbox);
    std::filesystem::create_directories(root);

    // Nothing authored: the stock boot path stays the untouched cartridge.
    tracks::scan(root);
    require(publish_dkrmap_artwork(stock).empty(), "Artwork appeared without a track.");
    require(AssetBank::append_textures(stock, {}) == stock, "An empty artwork list changed the bank.");
    require(!RuntimeSession(stock).acquire().route(), "A session without additions left the cartridge path.");

    const std::vector<AuthoredTrack> authored{
        {"alpha", {texture(32, 16, 'a'), texture(16, 16, 'b')}},
        {"beta", {texture(64, 32, 'c')}},
    };
    for(const auto& track : authored) write_track(root, track);
    tracks::scan(root);
    require(tracks::tracks().size() == authored.size(), "The authored tracks were not scanned.");

    // A character namespace owning one texture and one model, built the way
    // allocate_characters' additions are.
    const auto character_texture = texture(16, 16, 'k');
    const AssetBank::Overrides additions{{{2, static_cast<unsigned>(n2)}, character_texture},
                                         {{29, static_cast<unsigned>(n29)}, Bytes(64, 0x5A)}};
    const auto original_ids = stock->stock_section(30);
    Bytes ids(original_ids.begin(), original_ids.begin() + (n29 + 1) * 2);
    ids.push_back(static_cast<std::uint8_t>(n32 >> 8));
    ids.push_back(static_cast<std::uint8_t>(n32));
    const auto characters = AssetBank::augment(stock, additions, ids);
    require(characters->record_count(2) == n2 + 1, "The character texture was not appended.");

    // Characters first, dkrmap artwork after them, in custom_tracks' order.
    const auto artwork = publish_dkrmap_artwork(characters);
    require(artwork.size() == 3, "Not every authored texture was published.");
    const auto boot = AssetBank::append_textures(characters, artwork);
    require(boot->augmented() && boot->record_count(2) == n2 + 1 + artwork.size(), "Boot artwork count is wrong.");
    require(std::ranges::equal(boot->record(2, static_cast<unsigned>(n2)), character_texture),
            "dkrmap artwork displaced a character texture.");
    require(boot->record_count(29) == n29 + 1 && boot->record(29, static_cast<unsigned>(n29)).size() == 64,
            "dkrmap artwork dropped a character model.");
    for(std::size_t j = 0; j < artwork.size(); ++j)
        require(std::ranges::equal(boot->record(2, static_cast<unsigned>(n2 + 1 + j)), artwork[j]),
                "Boot bank artwork is out of order.");
    // The models resolve against the final bank too, not just the table.
    require_models_resolve(boot, authored, n2 + 1);
    {
        const auto directory = AssetDirectory::build(boot);
        const auto table = directory->read(3, 0, directory->section_size(3));
        const auto count = boot->record_count(2);
        require(be32(table, 4 * count) == directory->section_size(2), "Texture table does not span the artwork.");
        require(be32(table, 4 * (count + 1)) == 0xFFFFFFFFU, "Texture table sentinel is misplaced.");
    }

    // Every scene carries the same artwork at the same IDs.
    AssetBus bus;
    require(bool(ResidentBank::prepare(boot, boot, bus)->route()), "The artwork boot bank does not route.");
    const auto course = AssetBank::derive(stock, std::string(64, 'a'), {{{27, 5}, Bytes{1, 2, 3}}});
    const auto scene = AssetBank::append_textures(AssetBank::augment(course, additions, ids), artwork);
    require(bool(ResidentBank::prepare(boot, scene, bus)->route()), "A legacy course lost the shared artwork.");
    require(scene->digest() == course->digest() && scene->record(27, 5)[0] == 1, "Artwork discarded a legacy course.");
    rejects([&] {ResidentBank::prepare(boot, AssetBank::augment(course, additions, ids), bus);},
            "A scene without the artwork was accepted.");
    rejects([&] {ResidentBank::prepare(characters, scene, bus);},
            "A scene grew the boot texture table.");
    auto reordered = artwork;
    std::reverse(reordered.begin(), reordered.end());
    rejects([&] {
        ResidentBank::prepare(boot, AssetBank::append_textures(AssetBank::augment(course, additions, ids), reordered), bus);
    }, "A scene moved a live artwork ID.");

    // Artwork alone, without characters, starts right after the cartridge's.
    const auto alone = publish_dkrmap_artwork(stock);
    require(alone == artwork, "Artwork bytes depend on the character namespace.");
    const auto stock_boot = AssetBank::append_textures(stock, alone);
    require(stock_boot->augmented() && std::ranges::equal(stock_boot->record(2, static_cast<unsigned>(n2)), alone[0]),
            "Artwork alone does not follow the cartridge textures.");
    require_models_resolve(stock_boot, authored, n2);
    RuntimeSession session(stock, nullptr, alone);
    require(bool(session.acquire().route()), "An artwork session read the cartridge's smaller table.");
    rejects([&] {ResidentBank::prepare(stock_boot, course, bus);}, "A course without the artwork was accepted.");

    std::filesystem::remove_all(sandbox);
}
}

int main(int argc, char** argv) {
    try {
        unit();
        if(argc == 2) corpus(utf8_path(argv[1]));
        else if(argc != 1) throw Error("Usage: tests [owned-v77-or-v80-rom]");
        std::cout << checks << " dkrmap artwork checks passed"
                  << (argc == 2 ? "." : "; pass an owned ROM for the bank checks.") << '\n';
        return 0;
    } catch(const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
