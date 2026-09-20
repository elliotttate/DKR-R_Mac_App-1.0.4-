#include "legacy_character_stage.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <numbers>

namespace dkr::mods {
namespace {
gpr ptr(std::uint32_t v){return static_cast<gpr>(static_cast<std::int32_t>(v));}
std::uint32_t invoke(OriginalPiHandler fn,std::span<std::uint8_t> memory,const recomp_context& saved,
    std::uint32_t a,std::uint32_t b=0) {
    if(!fn)throw Error("Native stage callback is missing.");
    auto ctx=saved;ctx.r4=ptr(a);ctx.r5=ptr(b);
    ctx.f_odd=ctx.mips3_float_mode?&ctx.f1.u32l:&ctx.f0.u32h;
    fn(memory.data(),&ctx);return static_cast<std::uint32_t>(ctx.r2);
}
void face_camera(CharacterMenuMemory& g,std::uint32_t object,std::uint32_t camera) {
    // Selection models face local -Z, not +Z. The authored stage puts its
    // camera at +Z and its actors near a half-turn (Conker ~193, Timber ~159
    // degrees). Preserve that convention while aiming the outer actors at the
    // actual camera. Only yaw changes; animation, scale and position do not.
    const double dx=static_cast<double>(std::bit_cast<float>(g.read(camera+0x0c)))-
        std::bit_cast<float>(g.read(object+0x0c));
    const double dz=static_cast<double>(std::bit_cast<float>(g.read(camera+0x14)))-
        std::bit_cast<float>(g.read(object+0x14));
    if(!std::isfinite(dx)||!std::isfinite(dz))throw Error("Invalid selection camera position.");
    if(dx*dx+dz*dz<0.000001)return; // No defined yaw when directly above the actor.
    const auto yaw=0x8000+static_cast<int>(std::lround(std::atan2(dx,dz)*32768.0/std::numbers::pi));
    g.write(object,static_cast<unsigned>(yaw)&0xffffU,2);
}
}
void CharacterStage::initialize(std::span<std::uint8_t> memory,const CharacterMenuFields& fields,recomp_context& ctx,
    const CharacterStageCalls& calls,const std::vector<AllocatedCharacter>& entries) {
    if(!actors_.empty() || pending_entry_)throw Error("Selection stage already owns actors.");
    // This candidate qualifies the supplied two additions. Larger stage layouts
    // need readable-space qualification, not hidden pages or model squashing.
    if(entries.size()>2)throw Error("This private native-stage candidate is qualified for two additional actors only.");
    CharacterMenuMemory g(memory,fields);
    fields_=fields;retiring_.clear();
    if(g.get(CharacterMenuField::ObjectCount)>512-entries.size())
        throw Error("Not enough native object-list slots for the selection actors.");
    const auto sp=static_cast<std::uint32_t>(ctx.r29);
    if(sp%8 || (sp&0x1fffffffU)<0x400)throw Error("Invalid stage spawn stack.");
    const auto scratch=sp-0x100;g.bytes(scratch,Bytes(16));
    auto nested=ctx;nested.r29=ptr(sp-0x200);
    try {
        for(unsigned i=0;i<entries.size();++i) {
            const auto& c=entries[i];
            if(c.stage_source_header>=512 || !c.stage_header || c.stage_header==c.stage_source_header || c.stage_header>=32768)
                throw Error("Selection actor has invalid isolated header IDs.");
            // The reviewed menu model keeps its original base scale, all axes.
            // Place additions at the outer ends of the existing lower row.
            const float x=i==0?-52.0f:65.0f;
            g.write(scratch,c.stage_source_header&255,1);
            g.write(scratch+1,8|((c.stage_source_header>>1)&128),1);
            g.write(scratch+2,static_cast<unsigned>(static_cast<int>(x)),2);
            g.write(scratch+4,static_cast<unsigned>(-34),2);g.write(scratch+6,35,2);
            pending_entry_=scratch;pending_header_=c.stage_header;pending_source_=c.stage_source_header;
            const auto object=invoke(calls.spawn,memory,nested,scratch,3); // direct header + original object list
            pending_entry_=pending_header_=0;
            if(!object)throw Error("Not enough native object/model memory for the selection actor.");
            actors_.push_back({object});
            g.write(object+0x3c,0); // stack entry must never survive its call
            g.write(object,0x8000,2);g.write(object+2,0,2);g.write(object+4,0,2);
            const auto header=g.read(object+0x40);
            const float base_scale=std::bit_cast<float>(g.read(header+0x0c));
            if(!std::isfinite(base_scale) || base_scale<=0 || base_scale>10)throw Error("Invalid selection model scale.");
            const float entry_scale=c.base_character==3?0.71875f:0.671875f;
            g.write(object+8,std::bit_cast<std::uint32_t>(base_scale*entry_scale));
            g.write(object+0x3b,1,1);g.write(object+0x18,0,2);
            const auto animated=g.read(object+0x64);
            if(!animated)throw Error("Selection actor has no native animated behaviour allocation.");
            g.write(animated+0x28,100+i,2);g.write(animated+0x2c,1,1);g.write(animated+0x42,255,1);
            const auto model=g.read(g.read(g.read(object+0x68)));
            const auto batches=g.read(model+0x38),count=g.read(model+0x28,2);
            if(count>4096 || g.read(model+0x48,2)<2)throw Error("Invalid selection model animation/batch counts.");
            for(unsigned b=0;b<count;++b)if(g.read(batches+12*b,1)<4)actors_.back().sign_batches.push_back(b);
            if(actors_.back().sign_batches.empty())throw Error("Selection actor has no validated sign geometry.");
        }
    }catch(const Error&){pending_entry_=pending_header_=0;release(memory,ctx,calls);throw;}
}
bool CharacterStage::redirect_spawn(std::span<std::uint8_t> memory,recomp_context& ctx) {
    if(!pending_entry_)return false;
    const auto sp=static_cast<std::uint32_t>(ctx.r29);
    // A blank fields array is not needed here: read the checked spawn ABI only.
    const auto offset=(sp&0x1fffffffU)+0x68;
    if((sp&0xe0000000U)!=0x80000000U || offset>memory.size() || 4>memory.size()-offset)
        throw Error("Invalid stage spawn entry lifetime.");
    std::uint32_t entry=0;for(unsigned i=0;i<4;++i)entry=(entry<<8)|memory[(offset+i)^3];
    if(entry!=pending_entry_ || static_cast<unsigned>(ctx.r4)!=pending_source_)return false;
    ctx.r4=pending_header_;return true;
}
void CharacterStage::release(std::span<std::uint8_t> memory,recomp_context& ctx,const CharacterStageCalls& calls) {
    if(actors_.empty())return;
    CharacterMenuMemory g(memory,fields_);
    if(g.get(CharacterMenuField::FreeListCount)>200-actors_.size())
        throw Error("Native selection object cleanup queue is full.");
    for(const auto& actor:actors_) {
        // Native free_object queues destruction. Suppress any intervening
        // loop without dereferencing the retired actor or entering fixed tables.
        retiring_.push_back(actor.object);
        invoke(calls.free,memory,ctx,actor.object);
    }
    actors_.clear();
}
bool CharacterStage::update(std::span<std::uint8_t> memory,const CharacterMenuFields& fields,recomp_context& ctx,
    const CharacterStageCalls& calls,const CharacterMenuView& view) {
    const auto object=static_cast<std::uint32_t>(ctx.r4);
    if(std::ranges::find(retiring_,object)!=retiring_.end())return true;
    auto found=std::find_if(actors_.begin(),actors_.end(),[&](const auto& a){return a.object==object;});
    if(found==actors_.end())return false;
    auto& actor=*found;const auto id=static_cast<std::size_t>(found-actors_.begin());
    CharacterMenuMemory g(memory,fields);
    const auto camera=invoke(calls.camera,memory,ctx,0);
    // Validate the complete transform before offset arithmetic/read access.
    g.read(camera,4);g.read(camera+0x17,1);
    face_camera(g,object,camera);
    std::vector<unsigned> players;
    for(unsigned p=0;p<4;++p)if(view.active[p] && view.custom[p]==id)players.push_back(p);
    const auto dt=std::min(static_cast<unsigned>(ctx.r5),8U);
    actor.sign_ticks+=dt;
    if(actor.sign_ticks>=16){actor.sign_ticks&=15;++actor.sign_index;}
    if(actor.sign_index>=players.size())actor.sign_index=0;
    const unsigned animation=players.empty()?1:0;
    const auto model=g.read(g.read(g.read(object+0x68)));
    const auto batches=g.read(model+0x38);
    g.write(object+0x74,0);
    if(!players.empty()) {
        for(const auto b:actor.sign_batches)g.write(batches+12*b,players[actor.sign_index],1);
        for(const auto p:players)if(g.get(CharacterMenuField::Status,p,1)==1) {
            g.write(object+0x74,1);invoke(calls.particles,memory,ctx,object,2);
        }
    }
    g.write(object+0x3b,animation,1);
    const auto animations=g.read(model+0x44);
    const auto frames=g.read(animations+8*animation+4);
    if(!frames || frames>2048)throw Error("Selection animation exceeds native frame range.");
    if(!calls.animation_fraction || !calls.animation_override)throw Error("Selection music clock is unavailable.");
    auto music=ctx;
    music.f_odd=music.mips3_float_mode?&music.f1.u32l:&music.f0.u32h;
    calls.animation_fraction(memory.data(),&music);
    // The retail obj_loop_char_select call site applies this existing override
    // after audio-clock bookkeeping. Custom actors return before that call
    // site, so explicitly reuse it here: one shared phase, not a per-actor clock.
    calls.animation_override(memory.data(),&music);
    const float phase=music.f0.fl;
    if(!std::isfinite(phase) || phase<0 || phase>1)throw Error("Invalid selection music phase.");
    const float pingpong=phase>0.5f?2.0f*(1.0f-phase):2.0f*phase;
    g.write(object+0x18,static_cast<unsigned>((frames-1)*16*pingpong),2);
    return true;
}
}
