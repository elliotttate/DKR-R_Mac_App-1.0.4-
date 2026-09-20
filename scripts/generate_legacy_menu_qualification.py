#!/usr/bin/env python3
"""Regenerate private native-menu candidates and one-function test fixtures.

Uses only the existing checked Patch Pipeline and N64Recomp executable. No
generated C edits, no changes to the input ELF, ROM or versioned base policy.
"""
import argparse
from pathlib import Path
import subprocess
import sys
import json


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output",type=Path,required=True)
    parser.add_argument("--v77-build",type=Path,required=True)
    parser.add_argument("--v80-build",type=Path,required=True)
    parser.add_argument("--recompiler",type=Path,required=True)
    parser.add_argument("--characters",action="store_true",help="Include the checked character resource adapter")
    parser.add_argument("--character-menu",action="store_true",help="Include the checked additive character selector")
    parser.add_argument("--refresh-presentation",action="store_true",help="Refresh the reviewed presentation layer in an already-composed committed policy")
    parser.add_argument("--v77-policy",type=Path,help="Explicit reviewed baseline policy for v1.0")
    parser.add_argument("--v80-policy",type=Path,help="Explicit reviewed baseline policy for v1.1")
    args=parser.parse_args()
    root=Path(__file__).resolve().parents[1]
    output=args.output.resolve()
    if output==root or output==Path(output.anchor) or output.is_relative_to(root/"extern"):
        raise ValueError("Private generation must target a dedicated build directory")
    for revision,source,entry in ((77,args.v77_build,"0x80065D40"),(80,args.v80_build,"0x80065F80")):
        elf=source/f"dkr.us.v{revision}.elf"
        rom=source/f"dkr.us.v{revision}.z64"
        policy=output/f"menu-v{revision}.policy.json"
        source_policy=(args.v77_policy if revision==77 else args.v80_policy) or root/f'runtime-recomp/dkr.us.v{revision}.recomp-policy.json'
        compose_command=[sys.executable,str(root/"scripts/compose_legacy_mod_policy.py"),
            "--policy",str(source_policy),
            "--fragment",str(root/f"runtime-recomp/legacy-mods.v{revision}.recomp-fragment.json"),
            "--elf",str(elf),"--scene-runtime","--track-menu",str(root/f"runtime-recomp/legacy-track-menu.v{revision}.recomp-fragment.json"),
            "--output",str(policy)]
        if args.characters:compose_command += ['--characters',str(root/f'runtime-recomp/legacy-characters.v{revision}.recomp-fragment.json')]
        if args.character_menu:
            if not args.characters:raise ValueError('Character selector requires the resource adapter')
            compose_command += ['--character-menu',str(root/f'runtime-recomp/legacy-character-menu.v{revision}.recomp-fragment.json')]
        if args.refresh_presentation:
            if not args.character_menu:raise ValueError('Presentation refresh requires the character menu')
            from compose_legacy_mod_policy import elf_sections,elf_functions
            from legacy_character_presentation_policy import refresh_presentation
            base=json.loads(source_policy.read_text())
            updated=refresh_presentation(base,elf,f'us.v{revision}',elf_sections(elf),elf_functions(elf,(1,2)))
            policy.parent.mkdir(parents=True,exist_ok=True)
            policy.write_text(json.dumps(updated,indent=2)+'\n')
        else:subprocess.run(compose_command,check=True)
        for isolated in (True,False):
            functions=output/"menu-pipeline"/f"RecompiledFuncs-v{revision}" if isolated else output/f"generated-v{revision}"
            config=output/f"menu-{'pipeline-' if isolated else ''}v{revision}.toml"
            command=[sys.executable,str(root/"scripts/generate_recomp_config.py"),"--policy",str(policy),
                "--elf",str(elf),"--rom",str(rom),"--output-functions",str(functions),"--entrypoint",entry,"--output",str(config)]
            if isolated:command += ["--functions-per-output-file","1"]
            subprocess.run(command,check=True)
            subprocess.run([str(args.recompiler.resolve()),str(config)],check=True)


if __name__=="__main__":main()
