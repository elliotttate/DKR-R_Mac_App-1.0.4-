#include "texture_pack_browser_policy.hpp"

#include <cassert>

namespace {

dkr::runtime::texture_packs::PackInfo pack(
    const char* id, const char* name,
    dkr::runtime::texture_packs::Format format, bool enabled,
    bool compatible, bool hidden, std::uintmax_t size,
    std::int64_t imported,
    dkr::runtime::texture_packs::Origin origin =
        dkr::runtime::texture_packs::Origin::User) {
    dkr::runtime::texture_packs::PackInfo result{};
    result.id = id;
    result.name = name;
    result.format = format;
    result.enabled = enabled;
    result.compatible = compatible;
    result.hidden = hidden;
    result.managed_size_bytes = size;
    result.imported_at_unix_seconds = imported;
    result.origin = origin;
    return result;
}

} // namespace

int main() {
    using namespace dkr::runtime;
    using namespace texture_browser;
    const std::vector<texture_packs::PackInfo> packs{
        pack("charlie", "Charlie HD", texture_packs::Format::RiceRt64,
             true, true, false, 300U, 30),
        pack("alpha", "alpha native", texture_packs::Format::NativeRt64,
             false, true, false, 100U, 10),
        pack("bravo", "Bravo Jabo", texture_packs::Format::LegacyJabo,
             false, false, true, 200U, 20),
    };

    Filters filters{};
    assert(select(packs, filters, SortMode::NameAscending).size() == 2U);
    filters.visibility = VisibilityFilter::All;
    auto selected = select(packs, filters, SortMode::NameAscending);
    assert(selected[0].id == "alpha");
    assert(selected[1].id == "bravo");
    assert(selected[2].id == "charlie");

    selected = select(packs, filters, SortMode::SizeLargest);
    assert(selected[0].id == "charlie");
    selected = select(packs, filters, SortMode::ImportOldest);
    assert(selected[0].id == "alpha");

    filters.query = "RT64 BRIDGE";
    selected = select(packs, filters, SortMode::NameAscending);
    assert(selected.size() == 1U && selected[0].id == "charlie");
    filters.query.clear();
    filters.state = StateFilter::Active;
    assert(select(packs, filters, SortMode::NameAscending).size() == 1U);
    filters.state = StateFilter::All;
    filters.compatibility = CompatibilityFilter::Incompatible;
    selected = select(packs, filters, SortMode::NameAscending);
    assert(selected.size() == 1U && selected[0].id == "bravo");
    filters.compatibility = CompatibilityFilter::All;
    filters.format = texture_packs::Format::NativeRt64;
    selected = select(packs, filters, SortMode::NameAscending);
    assert(selected.size() == 1U && selected[0].id == "alpha");

    assert(responsive_column_count(259.0F, 8.0F) == 1);
    assert(responsive_column_count(528.0F, 8.0F) == 2);
    assert(responsive_column_count(1064.0F, 8.0F) == 4);
    assert(responsive_column_count(-1.0F, 8.0F) == 1);

    // A pack that came with a custom track is kept out of every ordinary view
    // and appears only under the "Track packs" filter.
    const std::vector<texture_packs::PackInfo> with_track_pack{
        pack("alpha", "alpha native", texture_packs::Format::NativeRt64,
             true, true, false, 100U, 10),
        pack("remix-hd", "Ancient Lake Remix HD",
             texture_packs::Format::RiceRt64, true, true, false, 500U, 40,
             texture_packs::Origin::TrackPack),
    };
    Filters track_filters{};
    assert(select(with_track_pack, track_filters,
                  SortMode::NameAscending).size() == 1U);
    assert(select(with_track_pack, track_filters,
                  SortMode::NameAscending)[0].id == "alpha");
    track_filters.visibility = VisibilityFilter::All;
    assert(select(with_track_pack, track_filters,
                  SortMode::NameAscending).size() == 1U);
    track_filters.visibility = VisibilityFilter::Hidden;
    assert(select(with_track_pack, track_filters,
                  SortMode::NameAscending).empty());
    track_filters.visibility = VisibilityFilter::TrackPacks;
    selected = select(with_track_pack, track_filters, SortMode::NameAscending);
    assert(selected.size() == 1U && selected[0].id == "remix-hd");
    // The other filters still narrow within the track-pack view.
    track_filters.state = StateFilter::Inactive;
    assert(select(with_track_pack, track_filters,
                  SortMode::NameAscending).empty());
    return 0;
}
