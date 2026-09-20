"""Checked, additive per-racer portrait/voice hooks; no generated C edits.

Only the two hash-pinned retail ELFs are admitted. The native lookup/call
instructions remain unchanged: callbacks redirect their local arguments.
"""
import copy
import hashlib
import struct

ELFS = {
    'us.v77': '15eb20705e4ccd8ad75c07f43f073d28674f0fe542bac721d32c0034395f4409',
    'us.v80': 'a3fd54e626af1e99a51751dafd0c6fa2498ea035722e1e4aed73df9cd94c7bbb',
}

# Pinned 06cf09b decomp rebuilt on macOS with IDO and MIPS binutils. Its v80
# output verifies against the canonical retail ROM (SHA1 6d96743d46f8c0cd0edb0ec5600b003c89b93755).
# Keep the whole-file pin and the instruction/function checks below.
MACOS_V80_ELF = '3f805aa9ef6f787c0865b1eae66683ca530ad3d36d94b1e8a8c01a962f962bd2'

def reviewed_retail_elf(elf, revision):
    digest = hashlib.sha256(elf.read_bytes()).hexdigest()
    return revision in ELFS and (digest == ELFS[revision] or
        (revision == 'us.v80' and digest == MACOS_V80_ELF))
# function, address, Settings+racer*24 register, lookup-address register, lw word
PORTRAITS = {
    'us.v77': [('postrace_load',0x80094ae8,10,13,0x8dae0000),
               ('postrace_load',0x80094b44,2,10,0x8d4b0000),
               ('results_render',0x80096b94,16,25,0x8f250000),
               ('func_80098774',0x80098984,3,25,0x8f2e0000),
               ('func_80098774',0x800989c0,15,14,0x8dcf0000)],
    'us.v80': [('postrace_load',0x80094fec,10,13,0x8dae0000),
               ('postrace_load',0x80095048,2,10,0x8d4b0000),
               ('results_render',0x800970bc,16,11,0x8d650000),
               ('func_80098774',0x80098ec0,3,25,0x8f2e0000),
               ('func_80098774',0x80098efc,15,14,0x8dcf0000)],
}
# Call site, owner expression, HUD argument adjustment, original delay slot.
HUD_CALLS = {
    'us.v77': [('hud_eggs_portrait',0x800a1a6c,'ctx->r19',0x640,0x24e70640),
               ('hud_lives_render',0x800a2380,'MEM_W(ctx->r29, 0x20)',0x640,0x24e70640),
               ('hud_treasure',0x800a46bc,'ctx->r22',0x640,0x24e70640),
               ('hud_main_hub',0x800a275c,'1U',0,0xa4f90006)],
    'us.v80': [('hud_eggs_portrait',0x800a1fac,'ctx->r19',0x640,0x24e70640),
               ('hud_lives_render',0x800a28c0,'MEM_W(ctx->r29, 0x20)',0x640,0x24e70640),
               ('hud_treasure',0x800a4c04,'ctx->r22',0x640,0x24e70640),
               ('hud_main_hub',0x800a2c9c,'1U',0,0xa4f90006)],
}
# PAL, unit-scale (batched/immediate), and arbitrary-scale texture lookups.
# Finish-position digits and all layout instructions remain unmodified.
HUD_LOADS = {'us.v77':[(0x800aab2c,12,0x8d820000),(0x800aabd8,14,0x8dc30000),(0x800aad30,24,0x8f020000)],
             'us.v80':[(0x800ab088,12,0x8d820000),(0x800ab134,14,0x8dc30000),(0x800ab28c,24,0x8f020000)]}
CINEMATIC = {'us.v77':(0x800993d8,0x8009ae94),'us.v80':(0x80099914,0x8009b3d0)}

def compose_presentation(policy, elf, revision, sections, symbols, *, extended=True):
    if not reviewed_retail_elf(elf, revision):
        raise ValueError('Custom presentation requires a reviewed retail ELF')
    result=copy.deepcopy(policy)
    words={base+i:struct.unpack_from('>I',data,i)[0] for base,data in sections for i in range(0,len(data),4)}
    def one(name):
        values=symbols.get(name,set())
        if len(values)!=1:raise ValueError('Ambiguous presentation function '+name)
        return next(iter(values))
    def hook(name,pc,text,expected):
        start,size=one(name)
        if pc%4 or not start<=pc<start+size or words.get(pc)!=expected:
            raise ValueError('Presentation instruction changed: '+name)
        if any(int(p['vram'],0)==pc for p in result.get('instructionPatches',[])):
            raise ValueError('Presentation overlaps an instruction patch')
        owners=[h for h in result['functionHooks'] if int(h['beforeVram'],0)==pc]
        if owners:
            # The only shared entry is the already-validated selection-sound
            # adapter. Its handle-scoped path remains first and unchanged.
            if name!='sound_play' or len(owners)!=1 or owners[0]['function']!=name or \
               not owners[0]['text'].startswith('{ extern int dkr_legacy_character_menu(') or \
               not owners[0]['text'].endswith('8U, dkr_character_menu_fields)) return; }'):
                raise ValueError('Unreviewed presentation hook owner: '+name)
            owners[0]['text']+=' '+text
        else:
            result['functionHooks'].append({'function':name,'beforeVram':hex(pc),'text':text,
                'reason':'Session-owned custom portraits/voices for committed human racers only; preserve native layout, RNG, gating and audio lifetimes.'})
    for name,pc,subject,lookup,expected in PORTRAITS[revision]:
        hook(name,pc,'{ extern uint32_t dkr_legacy_character_portrait_lookup(uint8_t*, recomp_context*, uint32_t); '
             f'uint32_t cell = dkr_legacy_character_portrait_lookup(rdram, ctx, (uint32_t)ctx->r{subject}); '
             f'if (cell) ctx->r{lookup} = (int32_t)cell; }}',expected)
    voice,horn=(0x800571f0,0x8005708c) if revision=='us.v77' else (0x80057230,0x800570cc)
    bonus,banana=(0x8003b30c,0x8003db10) if revision=='us.v77' else (0x8003b34c,0x8003db50)
    voices=[
        ('play_random_character_voice',voice,'audspat_play_sound_at_position',18,8,0x3104ffff),
        ('racer_play_sound',horn,'sound_play_spatial',2,4,0xafa00010)]
    if extended:voices += [
        ('obj_loop_bonus',bonus,'sound_play_spatial',17,9,0x01202025),
        ('obj_loop_banana',banana,'sound_play_spatial',8,4,0xafa00010)]
    for name,pc,target,racer,value,delay in voices:
        if words.get(pc+4)!=delay:raise ValueError('Race voice delay-slot ownership changed')
        hook(name,pc,'{ extern unsigned dkr_legacy_character_race_sound(uint8_t*, recomp_context*, uint32_t, unsigned); '
             f'ctx->r{value} = dkr_legacy_character_race_sound(rdram, ctx, (uint32_t)ctx->r{racer}, (unsigned)ctx->r{value}); }}',
             0x0c000000|((one(target)[0]>>2)&0x3ffffff))
    if extended and words.get(one('hud_element_render')[0])!=0x27bdff48:
        raise ValueError('HUD renderer stack ownership changed')
    for name,pc,owner,offset,delay in (HUD_CALLS[revision] if extended else []):
        if words.get(pc+4)!=delay:raise ValueError('HUD portrait delay-slot ownership changed')
        hook(name,pc,'{ extern void dkr_legacy_character_hud_bind(uint8_t*, recomp_context*, uint32_t, uint32_t); '
             f'dkr_legacy_character_hud_bind(rdram, ctx, (uint32_t)ctx->r7 + {offset}U, (uint32_t)({owner})); }}',
             0x0c000000|((one('hud_element_render')[0]>>2)&0x3ffffff))
        hook(name,pc+8,'{ extern void dkr_legacy_character_hud_unbind(uint8_t*, recomp_context*); '
             'dkr_legacy_character_hud_unbind(rdram, ctx); }',words[pc+8])
    for pc,lookup,expected in (HUD_LOADS[revision] if extended else []):
        hook('hud_element_render',pc,'{ extern uint32_t dkr_legacy_character_hud_lookup(uint8_t*, recomp_context*, uint32_t, uint32_t); '
             'uint32_t cell = dkr_legacy_character_hud_lookup(rdram, ctx, (uint32_t)ctx->r29 + 0xB8U, (uint32_t)ctx->r16); '
             f'if (cell) ctx->r{lookup} = (int32_t)cell; }}',expected)
    if extended:
        capture,draw=CINEMATIC[revision]
        if words.get(capture-4)!=0x80820059:raise ValueError('Cinematic owner load changed')
        hook('menu_trophy_race_rankings_loop',capture,
             '{ extern unsigned dkr_legacy_character_cinematic_id(uint8_t*, recomp_context*, unsigned, unsigned); '
             'ctx->r2 = dkr_legacy_character_cinematic_id(rdram, ctx, (unsigned)ctx->r14, (unsigned)ctx->r2); }',0x14600005)
        hook('menu_cinematic_loop',draw,
             '{ extern uint32_t dkr_legacy_character_cinematic_portrait(uint8_t*, recomp_context*, unsigned); '
             'uint32_t cell = dkr_legacy_character_cinematic_portrait(rdram, ctx, (unsigned)ctx->r17); '
             'if (cell) ctx->r24 = (int32_t)cell; }',0x8f050000)
    for kind,name in enumerate(('audspat_play_sound_at_position','sound_play_direct','sound_play')):
        pc=one(name)[0]
        hook(name,pc,'{ extern int dkr_legacy_character_play_sound(uint8_t*, recomp_context*, unsigned); '
             f'if (dkr_legacy_character_play_sound(rdram, ctx, {kind}U)) return; }}',words[pc])
    return result

def refresh_presentation(policy, elf, revision, sections, symbols):
    """Replace only an exact, known presentation layer in a composed policy.

    The committed base now includes the legacy adapter. Re-composing that whole
    adapter would duplicate its hooks. Strip only this layer's verified text,
    preserving shared owners and every unrelated patch byte-for-byte.
    """
    callbacks=('dkr_legacy_character_portrait_lookup','dkr_legacy_character_race_sound',
               'dkr_legacy_character_play_sound','dkr_legacy_character_hud_','dkr_legacy_character_cinematic_')
    if not any(any(c in h['text'] for c in callbacks) for h in policy['functionHooks']):
        return compose_presentation(policy,elf,revision,sections,symbols)
    for extended in (True,False):
        expected=compose_presentation({'functionHooks':[],'instructionPatches':[]},elf,revision,sections,symbols,extended=extended)
        result=copy.deepcopy(policy)
        for old in expected['functionHooks']:
            matches=[h for h in result['functionHooks'] if h['function']==old['function'] and int(h['beforeVram'],0)==int(old['beforeVram'],0)]
            if len(matches)!=1:break
            h=matches[0]
            if h['text']==old['text']:result['functionHooks'].remove(h)
            elif old['function']=='sound_play' and h['text'].endswith(' '+old['text']):
                h['text']=h['text'][:-len(old['text'])-1]
            else:break
        else:return compose_presentation(result,elf,revision,sections,symbols)
    raise ValueError('Existing custom presentation is not an exact reviewed layer')
