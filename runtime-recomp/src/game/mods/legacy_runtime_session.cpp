#include "legacy_runtime_session.hpp"
#include <limits>

namespace dkr::mods {
RuntimeSession::RuntimeSession(std::shared_ptr<const AssetBank> stock,std::shared_ptr<const CharacterNamespace> characters,
    std::vector<Bytes> shared_textures)
    :stock_(std::move(stock)),characters_(std::move(characters)),shared_textures_(std::move(shared_textures)),
     boot_(AssetBank::append_textures(characters_?characters_->apply(stock_):stock_,shared_textures_)),
     original_(ResidentBank::prepare(boot_,boot_,bus_)),resident_(original_) {
    if(characters_)for(const auto& character:characters_->characters) {
        // A private sample mount is never published as the active asset bank.
        // The original global music/SFX tables remain completely untouched.
        const auto bank=AssetBank::derive(stock_,character.id,{{{39,3},character.audio.samples}});
        character_audio_mounts_.push_back(bus_.mount(AssetDirectory::build(bank)));
        if(character.race_audio.control.empty())character_race_audio_mounts_.push_back(nullptr);
        else {
            validate_character_race_audio(character.race_audio);
            const auto race=AssetBank::derive(stock_,character.id,{{{39,3},character.race_audio.samples}});
            character_race_audio_mounts_.push_back(bus_.mount(AssetDirectory::build(race)));
        }
    }
}
std::uint32_t RuntimeSession::character_sample_address(unsigned index)const {
    std::size_t offset=0;for(unsigned i=0;i<3;++i)offset+=stock_->record(39,i).size();
    return character_audio_mounts_.at(index)->address(39,offset);
}
std::uint32_t RuntimeSession::character_race_sample_address(unsigned index)const {
    const auto& mount=character_race_audio_mounts_.at(index);if(!mount)return 0;
    std::size_t offset=0;for(unsigned i=0;i<3;++i)offset+=stock_->record(39,i).size();
    return mount->address(39,offset);
}
void RuntimeSession::admit(PreparedTrack track) {
    std::lock_guard lock(mutex_);
    if(scenes_ || requested_) throw Error("Scene content cannot be admitted after guest loading begins.");
    if(!track.bank || track.bank->base_fingerprint()!=stock_->fingerprint() ||
       track.bank->digest()!=track.root.content_id || track.artifact_digest.size()!=64)
        throw Error("Scene content does not match this session's verified Game Pak.");
    const auto id=track.root.content_id;
    if(auto found=admitted_.find(id);found!=admitted_.end()) {
        if(found->second.track.bank->fingerprint()!=track.bank->fingerprint())
            throw Error("Two different course banks claim the same content identity.");
        return;
    }
    auto resident=ResidentBank::prepare(boot_,AssetBank::append_textures(
        characters_?characters_->apply(track.bank):track.bank,shared_textures_),bus_);
    admitted_.emplace(id,Entry{std::move(track),std::move(resident)});
}
void RuntimeSession::request(std::string id,unsigned carrier) {
    std::lock_guard lock(mutex_);
    if(carrier>=stock_->record_count(23)) throw Error("Scene request has an invalid retail carrier.");
    if(!id.empty()) {
        const auto found=admitted_.find(id);
        if(found==admitted_.end() || found->second.track.root.carrier!=carrier)
            throw Error("Scene request refers to unprepared content or a different carrier.");
    }
    requested_=Request{std::move(id),carrier};
}
void RuntimeSession::begin_scene(std::span<std::uint8_t> guest,unsigned carrier,bool external_course) {
    std::lock_guard lock(mutex_);
    if(scenes_==std::numeric_limits<std::uint64_t>::max())throw Error("Scene generation exhausted.");
    // A request only belongs to its exact load; unrelated menus/cutscenes are
    // always original. New custom loads need a fresh explicit request.
    std::string next;
    if(requested_ && !external_course) {
        if(requested_->carrier==carrier)next=requested_->id;
        else if(!requested_->id.empty())throw Error("Pending custom scene does not match the actual scene load.");
    }
    if(next!=current_) {
        const auto bank=next.empty()?original_:admitted_.at(next).resident;
        auto plan=resident_.prepare(guest,bank);
        const auto result=resident_.commit(guest,std::move(plan));
        if(result!=ResidentAssetState::Commit::Published)
            throw Error(result==ResidentAssetState::Commit::Busy?
                "Scene loading crossed an outstanding asset read; custom content was not published.":
                "Scene loading changed resident tables during custom-content preparation.");
        current_=std::move(next);
    }
    requested_.reset();++scenes_;
}
ResidentAssetState::Lease RuntimeSession::acquire(){return resident_.acquire();}
std::string RuntimeSession::current_content()const{std::lock_guard lock(mutex_);return current_;}
std::uint64_t RuntimeSession::published_scenes()const{std::lock_guard lock(mutex_);return scenes_;}
std::vector<Root> RuntimeSession::tracks()const {
    std::lock_guard lock(mutex_);std::vector<Root> result;result.reserve(admitted_.size());
    for(const auto& [id,entry]:admitted_)result.push_back(entry.track.root);
    return result;
}
} // namespace dkr::mods
