#include "legacy_character_presentation.hpp"
#include <algorithm>
#include <bit>
#include <cstring>
#include <iostream>
using namespace dkr::mods;
namespace {
Bytes memory(8*MiB);CharacterMenuFields fields=[] {CharacterMenuFields f;f.fill(0x80000000);return f;}();
CharacterMenuMemory g(memory,fields);
unsigned checks=0,allocations=0,next=0x80400000,plays=0,points=0,parameters=0;
std::vector<std::uint32_t> args;
void check(bool b){++checks;if(!b)throw Error("Presentation assertion "+std::to_string(checks));}
gpr ptr(unsigned p){return std::int32_t(p);}
void allocate(std::uint8_t*,recomp_context* c){
 check(c->f_odd==(c->mips3_float_mode?&c->f1.u32l:&c->f0.u32h));
 *c->f_odd=0x12345678; // Nested odd-register writes must not alias the intercepted caller.
 c->r2=ptr(next);next+=(unsigned(c->r4)+15)&~15U;++allocations;}
void texture(std::uint8_t*,recomp_context* c){c->r2=ptr(0x80300000+unsigned(c->r4)*32);g.write(unsigned(c->r2),0x28280000);}
void relocate(std::uint8_t*,recomp_context* c){g.write(unsigned(c->r4)+4,unsigned(c->r4)+8);}
void play(std::uint8_t*,recomp_context* c){++plays;args={unsigned(c->r4),unsigned(c->r5),unsigned(c->r6),unsigned(c->r7)};g.write(unsigned(c->r7),0x80380000);}
void parameter(std::uint8_t*,recomp_context* c){++parameters;check(c->r4==ptr(0x80380000));check(c->r5==8||c->r5==16);}
void point(std::uint8_t*,recomp_context* c){++points;args={unsigned(c->r4),unsigned(c->r5),unsigned(c->r6),unsigned(c->r7)};
 for(unsigned i=4;i<12;++i)args.push_back(g.read(unsigned(c->r29)+4*i));if(args[11])g.write(args[11],0x80370000);}
CharacterPresentationCalls calls{allocate,texture,relocate,play,parameter,point};
}
int main(){try {
 auto assets=std::make_shared<CharacterNamespace>();
 for(unsigned i=0;i<2;++i){AllocatedCharacter a;a.id=std::string(64,'a'+i);a.name="Custom";a.base_character=3;a.portrait=906+i;
  a.race_audio.control=Bytes(32);a.race_audio.samples=Bytes(48);
  for(unsigned k=0;k<18;++k)a.race_audio.cues[k]={{k+1,90,110,63},12,14000};assets->characters.push_back(a);}
 CharacterRoster roster(assets);roster.request(0,assets->characters[0].id);roster.request(1,assets->characters[1].id);
 roster.commit(std::array<std::uint8_t,4>{3,3,3,9},4);
 CharacterPresentation p;recomp_context ctx{};ctx.r29=ptr(0x807f0000);ctx.r16=0x1234;
 ctx.f_odd=&ctx.f0.u32h;
 const auto original=ctx;p.initialize(memory,ctx,calls,assets->characters,std::array<std::uint32_t,2>{0x18000000,0x19000000});
 check(std::memcmp(&ctx,&original,sizeof(ctx))==0);check(allocations==4);
 p.initialize(memory,ctx,calls,assets->characters,std::array<std::uint32_t,2>{0x18000000,0x19000000});check(allocations==4);
 const auto a=p.portrait(roster,assets->characters,0),b=p.portrait(roster,assets->characters,1);
 check(a && b && a!=b);check(g.read(g.read(a))!=g.read(g.read(b)));
 for(auto cell:{a,b}){check(g.read(cell)==cell-16);check(g.read(cell-12)==0);check(g.read(cell-8)==0);check(g.read(cell-4)==0);}
 for(unsigned i=2;i<8;++i)check(!p.portrait(roster,assets->characters,i));
 check(p.cinematic_id(roster,assets->characters,0,3)==64);
 check(p.cinematic_id(roster,assets->characters,1,3)==65);
 for(unsigned i=2;i<8;++i)check(p.cinematic_id(roster,assets->characters,i,3)==3);
 check(p.cinematic_portrait(64)==a && p.cinematic_portrait(65)==b);
 for(unsigned id=0;id<256;++id)if(id<64 || id>=80)check(!p.cinematic_portrait(id));
 bool rejected=false;try{p.cinematic_portrait(66);}catch(const Error&){rejected=true;}check(rejected);
 p.bind_hud(0x807e0000,0x80310000,roster,assets->characters,0);
 check(p.hud_lookup(0x807e0000,0x80310000,59)==a-16);
 check(!p.hud_lookup(0x807e0000,0x80310000,60));
 check(!p.hud_lookup(0x807d0000,0x80310000,59));
 check(!p.hud_lookup(0x807e0000,0x80310020,59));
 p.bind_hud(0x807d0000,0x80310000,roster,assets->characters,1);
 check(p.hud_lookup(0x807d0000,0x80310000,59)==b-16);
 p.unbind_hud(0x807d0000);check(!p.hud_lookup(0x807d0000,0x80310000,59));
 check(p.hud_lookup(0x807e0000,0x80310000,59)==a-16);
 p.bind_hud(0x807e0000,0x80310000,roster,assets->characters,2); // Stock donor coexists.
 check(!p.hud_lookup(0x807e0000,0x80310000,59));
 p.bind_hud(0x807e0000,0x80310000,roster,assets->characters,65535); // AI.
 check(!p.hud_lookup(0x807e0000,0x80310000,59));
 for(unsigned player=0;player<8;++player)for(unsigned id=0;id<640;++id) {
  const auto result=p.sound(roster,assets->characters,player,3,id);const auto cue=character_race_cue(id,3);
  check(result==(player<2 && cue>=0?character_race_sound(player,cue):id));
  check(p.sound(roster,assets->characters,player,9,id)==id);
 }
 for(unsigned i=0;i<2;++i)for(unsigned c=0;c<18;++c) {
  ctx=original;ctx.r4=character_race_sound(i,c);ctx.r5=std::bit_cast<unsigned>(1.25f);ctx.r6=std::bit_cast<unsigned>(-2.5f);ctx.r7=std::bit_cast<unsigned>(3.75f);
  g.write(unsigned(ctx.r29)+16,4);g.write(unsigned(ctx.r29)+20,0x80360000);
  const auto before=ctx;check(p.play(memory,ctx,calls,assets->characters,0));check(std::memcmp(&ctx,&before,sizeof(ctx))==0);
  check(args==std::vector<unsigned>({character_race_sound(i,c),unsigned(ctx.r5),unsigned(ctx.r6),unsigned(ctx.r7),4,12,90,14000,0,110,63,0x80360000}));
  check(g.read(0x80360000)==0x80370000); // AudioPoint**, not SoundHandle*.
  ctx.r5=ptr(0x80360004);const auto params=parameters;
  check(p.play(memory,ctx,calls,assets->characters,1));check(args[1]==c+1 && args[2]==0 && args[3]==0x80360004);check(parameters==params);
  check(p.play(memory,ctx,calls,assets->characters,2));check(args[1]==c+1 && args[2]==63);check(parameters==params+2);
 }
 ctx=original;ctx.r4=0x165;check(!p.play(memory,ctx,calls,assets->characters,0));check(points==36 && plays==72);
 roster.clear_active();check(!p.portrait(roster,assets->characters,0));check(p.sound(roster,assets->characters,0,3,0x165)==0x165);
 check(p.cinematic_portrait(64)==a && p.cinematic_portrait(65)==b);
 check(p.cinematic_id(roster,assets->characters,0,3)==3);
 // Previously queued audio retains the correct immutable bank after selection changes.
 ctx.r4=character_race_sound(0,0);ctx.r5=0;check(p.play(memory,ctx,calls,assets->characters,1));check(args[3]==a+4);
 CharacterPresentation wide;ctx=original;ctx.mips3_float_mode=true;ctx.f_odd=&ctx.f1.u32l;
 const auto wide_original=ctx;wide.initialize(memory,ctx,calls,assets->characters,std::array<std::uint32_t,2>{0x18000000,0x19000000});
 check(std::memcmp(&ctx,&wide_original,sizeof(ctx))==0);
 std::cout<<checks<<" custom portrait/race-audio ownership checks passed.\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
