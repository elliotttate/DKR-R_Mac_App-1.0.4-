#include "legacy_character_roster.hpp"
#include <algorithm>
#include <set>

namespace dkr::mods {
CharacterRoster::CharacterRoster(std::shared_ptr<const CharacterNamespace> assets):assets_(std::move(assets)) {
    if(!assets_ || assets_->characters.empty())throw Error("Character roster needs a prepared asset namespace.");
}
void CharacterRoster::request(unsigned slot,const std::string& id) {
    if(slot>=4)throw Error("Character selection refers to an invalid player slot.");
    if(!id.empty() && std::none_of(assets_->characters.begin(),assets_->characters.end(),[&](const auto& c){return c.id==id;}))
        throw Error("Selected character is not in this boot's prepared namespace.");
    requested_[slot]=id;
}
void CharacterRoster::clear_active(){active_={};allow_duplicates_=false;}
void CharacterRoster::commit(std::span<const std::uint8_t> native,unsigned human_count) {
    if(human_count>4 || native.size()<human_count)throw Error("Invalid native human roster.");
    std::array<std::string,4> next{};
    std::set<std::string> used;
    for(unsigned i=0;i<human_count;++i) {
        if(native[i]>=10)throw Error("Native behaviour ID is outside the original roster.");
        if(requested_[i].empty())continue;
        const auto found=std::find_if(assets_->characters.begin(),assets_->characters.end(),[&](const auto& c){return c.id==requested_[i];});
        if(found==assets_->characters.end())throw Error("Logical character is missing at roster commit.");
        if(found->base_character!=native[i])
            throw Error("Logical character does not match the committed native behaviour (P"+std::to_string(i+1)+
                ", expected="+std::to_string(found->base_character)+", actual="+std::to_string(native[i])+").");
        if(!used.insert(found->id).second && !allow_duplicates_)throw Error("The same custom character was selected twice.");
        next[i]=found->id;
    }
    active_=std::move(next);
}
std::optional<unsigned> CharacterRoster::header(unsigned player,unsigned native_header) const {
    if(player>=4 || active_[player].empty())return std::nullopt;
    const auto found=std::find_if(assets_->characters.begin(),assets_->characters.end(),[&](const auto& c){return c.id==active_[player];});
    if(found==assets_->characters.end())throw Error("A committed character lost its immutable assets.");
    constexpr unsigned headers[]{2,3,4,5,6,7,8,9,1,0};
    if(found->base_character>=10)throw Error("Invalid inherited character behaviour.");
    for(unsigned vehicle=0;vehicle<3;++vehicle)
        if(native_header==headers[found->base_character]+vehicle*10)return found->headers[vehicle];
    return std::nullopt;
}
const std::string& CharacterRoster::active(unsigned slot) const {
    if(slot>=4)throw Error("Invalid character player slot.");return active_[slot];
}
} // namespace dkr::mods
