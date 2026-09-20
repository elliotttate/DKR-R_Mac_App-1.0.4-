#include "legacy_mod_library.hpp"

namespace dkr::mods {
void ModLibrary::configure(const std::filesystem::path& root,const std::filesystem::path& worker) {
    imports_.configure(root,worker);tracks_.configure(root,worker);characters_.configure(root,worker);
    phase_=Phase::Scan;modal_=false;success_=false;cancelled_=false;result_.clear();
}
ModLibraryView ModLibrary::snapshot() const {
    ModLibraryView out;
    out.imports=imports_.snapshot();out.tracks=tracks_.snapshot();out.characters=characters_.snapshot();
    out.busy=phase_!=Phase::Idle;out.modal=modal_;out.succeeded=success_;out.result=result_;
    if(phase_==Phase::Import) {
        out.stage=out.imports->stage;out.completed=out.imports->patch;out.total=out.imports->count;
    } else if(phase_==Phase::Tracks || phase_==Phase::ToggleTracks) {
        out.stage=out.tracks->stage;out.completed=out.tracks->completed;out.total=out.tracks->total;
    } else if(phase_==Phase::Characters || phase_==Phase::ToggleCharacters) {
        out.stage=out.characters->stage;out.completed=out.characters->completed;out.total=out.characters->total;
    } else out.stage=out.busy?"Reading mod libraries":"Mod library ready";
    return out;
}
void ModLibrary::finish(bool success,std::string message) {
    if(!success&&(phase_==Phase::ToggleTracks||phase_==Phase::ToggleCharacters))modal_=true;
    phase_=Phase::Idle;success_=success;result_=std::move(message);roms_.clear();
    imports_.dismiss();tracks_.dismiss();characters_.dismiss();
}
void ModLibrary::advance() {
    if(cancelled_) {finish(false,result_+"\nCancelled. Completed imports remain installed but were not enabled.");return;}
    if(need_tracks_) {
        need_tracks_=false;phase_=Phase::Tracks;
        if(!tracks_.prepare_review(review_,roms_))finish(false,"Could not start track preparation. Please refresh and retry.");
    } else if(need_characters_) {
        need_characters_=false;phase_=Phase::Characters;
        if(!characters_.prepare_review(review_,roms_))finish(false,"Could not start character preparation. Please refresh and retry.");
    } else finish(success_,result_.empty()?"No supported tracks or characters were identified.":result_+"\nChoose which prepared mods to enable in the libraries below.");
}
void ModLibrary::tick() {
    if(phase_==Phase::Idle)return;
    const auto state=snapshot();
    if(phase_==Phase::Scan) {
        if(state.imports->busy || state.tracks->busy || state.characters->busy)return;
        std::string errors;
        if(!state.imports->succeeded)errors+=state.imports->result+"\n";
        if(!state.tracks->succeeded)errors+=state.tracks->result+"\n";
        if(!state.characters->succeeded)errors+=state.characters->result+"\n";
        finish(errors.empty(),errors.empty()?"Mod libraries refreshed.":errors);
    } else if(phase_==Phase::Import) {
        if(state.imports->busy)return;
        if(!state.imports->succeeded){finish(false,state.imports->result);return;}
        review_=state.imports->imported_review;
        need_tracks_=state.imports->imported_tracks;need_characters_=state.imports->imported_characters;
        success_=need_tracks_ || need_characters_;advance();
    } else {
        const auto catalog=(phase_==Phase::Tracks || phase_==Phase::ToggleTracks)?state.tracks:state.characters;
        if(catalog->busy)return;
        if(phase_==Phase::ToggleTracks || phase_==Phase::ToggleCharacters){finish(catalog->succeeded,catalog->result);return;}
        if(!result_.empty())result_+='\n';
        result_+=(phase_==Phase::Tracks?"Tracks: ":"Characters: ")+catalog->result;
        success_ &= catalog->succeeded;
        // A rejected track does not discard a separately valid character (or
        // vice versa). Neither phase activates anything automatically.
        advance();
    }
}
bool ModLibrary::import_file(std::filesystem::path source,std::vector<std::filesystem::path> roms) {
    if(phase_!=Phase::Idle)return false;
    if(!imports_.import_file(std::move(source),roms))return false;
    roms_=std::move(roms);result_.clear();modal_=true;cancelled_=false;success_=false;phase_=Phase::Import;return true;
}
bool ModLibrary::prepare_review(std::string review,std::vector<std::filesystem::path> roms) {
    if(phase_!=Phase::Idle)return false;
    need_tracks_=need_characters_=false;
    for(const auto& item:imports_.snapshot()->items)if(item.review==review) {
        need_tracks_ |= item.kind=="Track";need_characters_ |= item.kind=="Character";
    }
    if(!need_tracks_ && !need_characters_)return false;
    review_=std::move(review);roms_=std::move(roms);result_.clear();modal_=true;cancelled_=false;success_=true;advance();return true;
}
bool ModLibrary::set_enabled(TrackCatalog::Kind kind,std::string id,bool enabled) {
    if(phase_!=Phase::Idle)return false;
    auto& catalog=kind==TrackCatalog::Kind::Track?tracks_:characters_;
    if(!catalog.set_enabled(std::move(id),enabled))return false;
    phase_=kind==TrackCatalog::Kind::Track?Phase::ToggleTracks:Phase::ToggleCharacters;
    modal_=false;cancelled_=false;result_.clear();return true;
}
bool ModLibrary::refresh() {
    if(phase_!=Phase::Idle)return false;
    imports_.refresh();tracks_.refresh();characters_.refresh();phase_=Phase::Scan;result_.clear();return true;
}
bool ModLibrary::set_hidden(TrackCatalog::Kind kind,std::string id,bool hidden) {
    if(phase_!=Phase::Idle)return false;
    auto& catalog=kind==TrackCatalog::Kind::Track?tracks_:characters_;
    if(!catalog.set_hidden(std::move(id),hidden))return false;
    phase_=kind==TrackCatalog::Kind::Track?Phase::ToggleTracks:Phase::ToggleCharacters;
    modal_=false;cancelled_=false;result_.clear();return true;
}
bool ModLibrary::remove(TrackCatalog::Kind kind,std::string id) {
    if(phase_!=Phase::Idle)return false;
    auto& catalog=kind==TrackCatalog::Kind::Track?tracks_:characters_;
    if(!catalog.remove(std::move(id)))return false;
    phase_=kind==TrackCatalog::Kind::Track?Phase::ToggleTracks:Phase::ToggleCharacters;
    modal_=true;cancelled_=false;result_.clear();return true;
}
bool ModLibrary::disable_all(TrackCatalog::Kind kind) {
    if(phase_!=Phase::Idle)return false;
    auto& catalog=kind==TrackCatalog::Kind::Track?tracks_:characters_;
    if(!catalog.disable_all())return false;
    phase_=kind==TrackCatalog::Kind::Track?Phase::ToggleTracks:Phase::ToggleCharacters;
    modal_=false;result_.clear();return true;
}
void ModLibrary::cancel(){cancelled_=true;imports_.cancel();tracks_.cancel();characters_.cancel();}
void ModLibrary::dismiss(){if(phase_==Phase::Idle)modal_=false;}
}
