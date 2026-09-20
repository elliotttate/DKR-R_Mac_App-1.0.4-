#include "legacy_track_menu_adapter.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <set>

namespace dkr::mods {
namespace {
class Guest {
    std::span<std::uint8_t> memory_;
    const TrackMenuFields& fields_;
public:
    Guest(std::span<std::uint8_t> memory,const TrackMenuFields& fields):memory_(memory),fields_(fields) {
        if(memory.size()%4)throw Error("Unaligned custom menu guest memory.");
        for(auto address:fields)check(address,4);
    }
    std::size_t check(std::uint32_t address,std::size_t size)const {
        auto p=std::size_t(address&0x1fffffffU);
        if((address&0xe0000000U)!=0x80000000U || p>memory_.size() || size>memory_.size()-p)
            throw Error("Custom menu accessed an invalid guest range.");
        return p;
    }
    std::uint32_t address(MenuField field,unsigned offset=0)const {
        const auto base=fields_.at(static_cast<unsigned>(field));
        if(offset>0xffffffffU-base)throw Error("Custom menu guest address overflow.");
        return base+offset;
    }
    std::uint32_t read(MenuField field,unsigned offset=0,unsigned size=4)const {
        auto p=check(address(field,offset),size);std::uint32_t result=0;
        for(unsigned i=0;i<size;++i)result=(result<<8)|memory_[(p+i)^3];return result;
    }
    int integer(MenuField field)const{return static_cast<std::int32_t>(read(field));}
    float real(MenuField field)const {
        const auto value=std::bit_cast<float>(read(field));
        if(!std::isfinite(value) || std::abs(value)>1000000)throw Error("Invalid native track-menu scroll coordinate.");
        return value;
    }
    void write(MenuField field,std::uint32_t value,unsigned offset=0,unsigned size=4) {
        auto p=check(address(field,offset),size);
        for(unsigned i=0;i<size;++i)memory_[(p+i)^3]=static_cast<std::uint8_t>(value>>((size-i-1)*8));
    }
    void real(MenuField field,float value){write(field,std::bit_cast<std::uint32_t>(value));}
};
constexpr std::uint32_t Confirm=0x9000,Back=0x4000;
}
TrackMenuAdapter::TrackMenuAdapter(std::vector<Root> tracks,bool allow_races)
    :tracks_(std::move(tracks)),allow_races_(allow_races) {
    if(tracks_.empty() || tracks_.size()>512)throw Error("Invalid native custom-track catalogue size.");
    std::set<std::string> identities;
    auto add_name=[&](std::string_view name) {
        if(name.empty() || name.size()>255)throw Error("Custom track name exceeds the native text budget.");
        for(unsigned char c:name)names_.push_back(c>=32 && c<127?c:'?');
        names_.push_back(0);
    };
    add_name("CUSTOM TRACKS");
    for(const auto& root:tracks_) {
        if(root.content_id.size()!=64 || !identities.insert(root.content_id).second || root.carrier>=128 ||
           !root.vehicles || (root.vehicles&~7U))throw Error("Invalid custom-track menu identity/capabilities.");
        name_offsets_.push_back(names_.size());add_name(root.name);
    }
    while(names_.size()%8)names_.push_back(0);
}
void TrackMenuAdapter::install_names(std::span<std::uint8_t> guest,std::uint32_t address) {
    if(name_address_)throw Error("Custom menu names are already installed for this guest.");
    const auto p=std::size_t(address&0x1fffffffU);
    if((address&0xe0000000U)!=0x80000000U || address%8 || guest.size()%4 || p>guest.size() || names_.size()>guest.size()-p)
        throw Error("Native custom-track names have no valid guest allocation.");
    for(std::size_t i=0;i<names_.size();++i)guest[(p+i)^3]=names_[i];name_address_=address;
}
TrackMenuEffect TrackMenuAdapter::apply(unsigned event,std::span<std::uint8_t> memory,
    const TrackMenuFields& fields,std::uint32_t arg,std::uint32_t returned) {
    if(event>17)throw Error("Unknown native custom-track event.");
    Guest guest(memory,fields);TrackMenuEffect effect;
    const unsigned stock_rows=static_cast<std::int16_t>(guest.read(MenuField::FutureFunLand,0,2))==-1?4:5;
    TrackMenuLayout layout(stock_rows,tracks_.size());
    auto cursor=[&](){return TrackCursor{guest.integer(MenuField::CursorY),guest.integer(MenuField::CursorX)};};
    auto root_at=[&](TrackCursor p)->const Root* {
        const auto cell=layout.cell(p);return cell && cell->custom_index?&tracks_.at(*cell->custom_index):nullptr;
    };
    auto choice=[&]()->TrackSceneChoice {
        if(const auto root=root_at(cursor()))return {root->content_id,root->carrier};
        return {"",static_cast<unsigned>(guest.integer(MenuField::PreviewCarrier))};
    };
    auto position=[&](TrackCursor p) {
        if(!layout.cell(p))throw Error("Custom selection escaped its logical menu.");
        const int height=guest.integer(MenuField::Height);
        if(height<120 || height>1024)throw Error("Invalid native menu viewport height.");
        guest.write(MenuField::CursorX,p.column);guest.write(MenuField::CursorY,p.row);
        guest.real(MenuField::TargetX,static_cast<float>(p.column*320));
        guest.real(MenuField::TargetY,static_cast<float>(-p.row*height));
        if(const auto root=root_at(p))guest.write(MenuField::PreviewCarrier,root->carrier);
        else {
            // The fifth stock row is an original retail allocation quirk;
            // never access it unless the original unlock/layout permits it.
            guest.write(MenuField::PreviewCarrier,static_cast<std::int16_t>(guest.read(MenuField::StockIDs,(p.row*6+p.column)*2,2)));
        }
        guest.write(MenuField::World,std::min(p.row+1,static_cast<int>(stock_rows)));
    };
    if(event==0) {
        in_tracks_=true;race_.reset();pending_.reset();candidate_.reset();preview_id_.clear();
        const auto previous=cursor();const bool custom=root_at(previous)!=nullptr;
        // Native init resets to Dino Domain after visiting the title screen.
        // Preserve custom selection only on a normal race/menu return, never
        // restore it over the original title-entry reset. Keep retail array
        // indexing safe in either case; the native function owns its flag.
        restore_=custom && !guest.integer(MenuField::TitleLoaded)?std::optional(previous):std::nullopt;
        if(custom) {guest.write(MenuField::CursorX,0);guest.write(MenuField::CursorY,0);}
        return effect;
    }
    if(event==1) {
        if(!name_address_)throw Error("Native custom-track names were not installed before menu drawing.");
        if(restore_) {
            position(*restore_);guest.real(MenuField::X,guest.real(MenuField::TargetX));
            guest.real(MenuField::Y,guest.real(MenuField::TargetY));guest.write(MenuField::Opacity,32);
            guest.write(MenuField::LoadedCarrier,-1);restore_.reset();
        }
        return effect;
    }
    if(event==15) {
        in_tracks_=arg==15;pending_.reset();candidate_.reset();preview_id_.clear();
        // Results may offer a retry of the same course. Retain its logical
        // identity there, but never let it follow a return to stock menus.
        if(arg!=17)race_.reset();
        return effect;
    }
    if(event==14) {
        if(race_ && race_->carrier==arg)effect.scene=*race_;
        else race_.reset();
        in_tracks_=false;return effect;
    }
    if(event==8) {
        if(pending_) {
            if(pending_->carrier!=arg)throw Error("Background preview identity no longer matches its accepted load.");
            effect.scene=*pending_;pending_.reset();
        }
        return effect;
    }
    if(!in_tracks_)return effect;
    if(event==2) {
        if(masked_)throw Error("Nested custom menu input.");
        const auto current=cursor();if(!layout.cell(current))throw Error("Native cursor is outside the custom menu.");
        const auto buttons=guest.read(MenuField::Buttons,16);
        const auto sx=static_cast<std::int16_t>(guest.read(MenuField::StickX,8,2));
        const auto sy=static_cast<std::int16_t>(guest.read(MenuField::StickY,8,2));
        const auto next=layout.move(current,sx,sy);
        const auto root=root_at(current);
        const bool custom_nav=root || root_at(next);
        const bool not_ready=root && (!allow_races_ || preview_id_!=root->content_id ||
            guest.integer(MenuField::BackgroundBusy) || guest.integer(MenuField::LoadedCarrier)!=static_cast<int>(root->carrier));
        if(custom_nav || not_ready) {
            masked_=true;saved_x_=sx;saved_y_=sy;saved_buttons_=buttons;
            guest.write(MenuField::StickX,0,8,2);guest.write(MenuField::StickY,0,8,2);
            if(not_ready)guest.write(MenuField::Buttons,buttons&~Confirm,16);
            if(guest.integer(MenuField::Delay)==0 && !(buttons&(Confirm|Back)) && custom_nav && next!=current)move_=next;
        }
    } else if(event==3) {
        if(masked_) {
            guest.write(MenuField::StickX,saved_x_,8,2);guest.write(MenuField::StickY,saved_y_,8,2);
            guest.write(MenuField::Buttons,saved_buttons_,16);masked_=false;
            if(move_) {position(*move_);move_.reset();effect.navigation_sound=true;}
        }
    } else if(event==4) {
        if(!name_address_)throw Error("Custom track names have not been initialized.");
        const auto height=guest.integer(MenuField::Height);
        if(height<120 || height>1024)throw Error("Invalid native menu viewport height.");
        const auto x=guest.real(MenuField::X),y=guest.real(MenuField::Y);
        const int cx=static_cast<int>(x/320),cy=static_cast<int>(y/-height);
        for(int row=-1,k=0;row<2;++row)for(int col=-1;col<2;++col,++k) {
            const TrackCursor p{cy+row,cx+col};const auto cell=layout.cell(p);if(!cell)continue;
            const auto offset=k*16U;
            if(cell->custom_index) {
                const auto index=*cell->custom_index;
                guest.write(MenuField::Details,name_address_,offset);
                guest.write(MenuField::Details,name_address_+static_cast<std::uint32_t>(name_offsets_[index]),offset+4);
                guest.write(MenuField::Details,static_cast<std::uint16_t>(static_cast<int>(p.column*320-x)),offset+8,2);
                guest.write(MenuField::Details,static_cast<std::uint16_t>(static_cast<int>(-p.row*height-y)),offset+10,2);
                guest.write(MenuField::Details,1,offset+12,1);
                const bool selected=p.column==guest.integer(MenuField::SelectedX) && p.row==guest.integer(MenuField::SelectedY);
                const auto opacity=selected?std::min(255,std::max(0,guest.integer(MenuField::Opacity))*8):255;
                guest.write(MenuField::Details,opacity,offset+13,1);
                guest.write(MenuField::Details,selected?128:0,offset+14,1);
                guest.write(MenuField::Details,4,offset+15,1);
            }
            // Only add adjacency to custom rows; preserve stock unlock badges,
            // texture/name pointers, frame style and all other native fields.
            if(cell->custom_index || p.row==static_cast<int>(stock_rows)-1) {
                const auto vp=guest.read(MenuField::Details,offset+14,1)&128;
                guest.write(MenuField::Details,vp|(guest.integer(MenuField::Delay)==0?layout.arrows(p):0),offset+14,1);
            }
        }
    } else if(event==5) {
        if(choice().id!=preview_id_)guest.write(MenuField::LoadedCarrier,-1);
    } else if(event==6) {
        if(candidate_)throw Error("Nested custom preview request.");
        auto selected=choice();
        if(selected.carrier==arg)candidate_=std::move(selected);
    } else if(event==7) {
        if(returned && candidate_) {
            if(pending_)throw Error("An accepted preview would replace an outstanding scene request.");
            pending_=candidate_;preview_id_=candidate_->id;
        }
        candidate_.reset();
    } else if(event==9 || event==10 || event==11) {
        const auto root=root_at(cursor());
        if(root && root->carrier==arg) {
            effect.override_return=true;
            if(event==9) {
                if(!name_address_)throw Error("Custom course names are unavailable.");
                const auto index=*layout.cell(cursor())->custom_index;
                effect.return_value=name_address_+static_cast<std::uint32_t>(name_offsets_[index]);
            } else if(event==11)effect.return_value=root->vehicles;
            else {unsigned vehicle=0;while(!(root->vehicles&(1U<<vehicle)))++vehicle;effect.return_value=vehicle;}
        }
    } else if(event==12 && arg==2) {
        const auto selected=choice();
        if(!selected.id.empty()) {
            if(!allow_races_)throw Error("Custom race activation has not passed the save-isolation qualification gate.");
            race_=selected;
        } else race_.reset();
    } else if(event==13) {
        if(root_at(cursor()))guest.write(MenuField::VoiceDelay,0);
    } else if(event==16) {
        if(background_y_)throw Error("Nested custom track-menu background draw.");
        const auto y=guest.real(MenuField::Y);
        const auto height=guest.integer(MenuField::Height);
        if(height<120 || height>1024)throw Error("Invalid native menu viewport height.");
        if(y<-(static_cast<int>(stock_rows)-1)*height) {
            background_y_=y;
            // Reuse the first world's authored background, not invalid world
            // texture indices. This scope never changes the live viewport.
            guest.real(MenuField::Y,0);
        }
    } else if(event==17 && background_y_) {
        guest.real(MenuField::Y,*background_y_);background_y_.reset();
    }
    return effect;
}
}
