#pragma once
#include "legacy_character_materialize.hpp"
#include <optional>

namespace dkr::mods {
// Selection identity is independent of both native behaviour ID and asset ID.
// This first adapter only substitutes explicitly committed human race slots.
// Bosses, AI, ghosts and arbitrary menu props never inherit the last selection.
class CharacterRoster {
public:
    explicit CharacterRoster(std::shared_ptr<const CharacterNamespace> assets);
    void request(unsigned slot,const std::string& id);
    void clear_active();
    void set_duplicate_policy(bool enabled){allow_duplicates_=enabled;}
    void commit(std::span<const std::uint8_t> native_characters,unsigned human_count);
    std::optional<unsigned> header(unsigned player,unsigned native_header) const;
    const std::string& active(unsigned slot) const;
private:
    std::shared_ptr<const CharacterNamespace> assets_;
    std::array<std::string,4> requested_{},active_{};
    bool allow_duplicates_=false;
};
} // namespace dkr::mods
