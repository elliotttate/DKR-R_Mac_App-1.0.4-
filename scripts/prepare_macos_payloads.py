#!/usr/bin/env python3
"""Generate both beta runtime payloads with the checked legacy-mod hooks."""

import argparse
import hashlib
from pathlib import Path
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--v77-build', type=Path, required=True)
    parser.add_argument('--v80-build', type=Path, required=True)
    parser.add_argument('--recompiler', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    for revision, source, entry, digest in (
        (77, args.v77_build, '0x80065D40', '0cb115d8716dbbc2922fda38e533b9fe63bb9670'),
        (80, args.v80_build, '0x80065F80', '6d96743d46f8c0cd0edb0ec5600b003c89b93755'),
    ):
        elf = (source / f'dkr.us.v{revision}.elf').resolve()
        rom = (source / f'dkr.us.v{revision}.z64').resolve()
        if hashlib.sha1(rom.read_bytes()).hexdigest() != digest:
            raise ValueError(f'v{revision} input must be the matching canonical retail ROM')
        policy = output / f'v{revision}.policy.json'
        command = [sys.executable, str(root / 'scripts/compose_legacy_mod_policy.py'),
            '--policy', str(root / f'runtime-recomp/dkr.us.v{revision}.recomp-policy.json'),
            '--elf', str(elf), '--scene-runtime', '--output', str(policy)]
        for flag, fragment in (('--fragment', 'legacy-mods'),
                               ('--track-menu', 'legacy-track-menu'),
                               ('--characters', 'legacy-characters'),
                               ('--character-menu', 'legacy-character-menu')):
            command += [flag, str(root / f'runtime-recomp/{fragment}.v{revision}.recomp-fragment.json')]
        subprocess.run(command, check=True)
        config = output / f'v{revision}.toml'
        subprocess.run([sys.executable, str(root / 'scripts/generate_recomp_config.py'),
            '--policy', str(policy), '--elf', str(elf), '--rom', str(rom),
            '--output-functions', str(output / f'generated-v{revision}'),
            '--entrypoint', entry, '--output', str(config)], check=True)
        subprocess.run([str(args.recompiler.resolve()), str(config)], check=True)
    print(f'Both payloads generated. Build with DKR_MAC_PAYLOAD_DIR="{output}" bash Build-macOS.sh')


if __name__ == '__main__':
    main()
