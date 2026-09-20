#include "legacy_runtime_character.hpp"

namespace dkr::mods {
void dispatch_character_event(CharacterRoster& roster,std::span<std::uint8_t> guest,
    recomp_context& ctx,unsigned event,std::uint32_t roster_address) {
    auto read=[&](std::uint32_t address,unsigned length) {
        const auto offset=address&0x1fffffffU;
        if((address&0xe0000000U)!=0x80000000U || guest.size()%4 || offset>guest.size() || length>guest.size()-offset)
            throw Error("Character hook received an invalid guest range.");
        std::uint32_t value=0;
        for(unsigned i=0;i<length;++i)value=(value<<8)|guest[(offset+i)^3];
        return value;
    };
    if(event==2)roster.clear_active();
    else if(event==1) {
        const auto count=static_cast<std::uint32_t>(ctx.r4);
        if(count>4 || roster_address>0xffffffffU-4)throw Error("Character commit has an invalid native player count/range.");
        std::array<std::uint8_t,4> ids{};
        for(unsigned i=0;i<count;++i)ids[i]=read(roster_address+i,1);
        roster.commit(ids,count);
    } else if(event==0) {
        const auto header=static_cast<std::uint32_t>(ctx.r4);
        if(header>=30)return;
        const auto stack=static_cast<std::uint32_t>(ctx.r29);
        if(stack>0xffffffffU-0x68)throw Error("Character stack range overflow.");
        const auto entry=read(stack+0x68,4);
        if(entry>0xffffffffU-16 || (read(entry+1,1)&0x7f)!=16)return;
        const auto player=read(entry+0xe,2);
        if(const auto mapped=roster.header(player,header))ctx.r4=*mapped;
    } else throw Error("Unknown character lifecycle event.");
}
}
