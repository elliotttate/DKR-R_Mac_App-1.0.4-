#!/usr/bin/env python3
"""Prepare the supplied SR.GU Rice pack, or verify the committed runtime pack.

Conversion needs Pillow; verification and ordinary builds use only Python's
standard library. The runtime reads the prepared pack without Python.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct

PATTERN = re.compile(r'#([0-9a-f]{8}#\d+#\d+(?:#[0-9a-f]{8})?)_(all|rgb|a)\s*\.png$', re.I)
MASK = (1 << 64) - 1


def native_alias(identity):
    value = 14695981039346656037
    for byte in ('dkr-r:rice:' + identity.lower()).encode('ascii'):
        value = ((value ^ byte) * 1099511628211) & MASK
    value ^= value >> 33
    value = (value * 0xff51afd7ed558ccd) & MASK
    value ^= value >> 33
    value = (value * 0xc4ceb9fe1a85ec53) & MASK
    value ^= value >> 33
    return f'{value:016x}'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False) + '\n')


def convert(source, output):
    from PIL import Image
    if output.exists():
        raise ValueError(f'Refusing to overwrite {output}')
    groups = {}
    ignored = []
    for path in sorted(source.rglob('*')):
        if not path.is_file():
            continue
        match = PATTERN.search(path.name)
        if not match:
            ignored.append({'path': path.relative_to(source).as_posix(),
                            'sha256': digest(path),
                            'reason': 'No Rice identity; not a runtime texture'})
            continue
        identity, variant = map(str.lower, match.groups())
        entries = groups.setdefault(identity, {})
        if variant in entries:
            raise ValueError(f'Duplicate Rice variant: {identity} {variant}')
        # Decode every supplied texture, even a variant superseded by _all.
        with Image.open(path) as image:
            image.load()
        entries[variant] = path
    if not groups:
        raise ValueError('No Rice textures found')
    output.mkdir(parents=True)
    records, textures = [], []
    aliases = set()
    merged = 0
    for identity, variants in sorted(groups.items()):
        name = f'Diddy Kong Racing#{identity}_all.png'
        destination = output / name
        if 'all' in variants:
            shutil.copyfile(variants['all'], destination)
            operation = 'all'
        elif 'rgb' in variants:
            with Image.open(variants['rgb']) as rgb:
                image = rgb.convert('RGBA')
            if 'a' in variants:
                with Image.open(variants['a']) as alpha:
                    if alpha.size != image.size:
                        raise ValueError(f'RGB/alpha dimensions differ: {identity}')
                    image.putalpha(alpha.convert('RGB').getchannel('R'))
                merged += 1
                operation = 'merge-rgb-alpha'
            else:
                image.putalpha(255)
                operation = 'opaque-rgb'
            image.save(destination)
        else:
            raise ValueError(f'Alpha without RGB: {identity}')
        with Image.open(destination) as image:
            dimensions = list(image.size)
        alias = native_alias(identity)
        if alias in aliases:
            raise ValueError(f'Native alias collision: {identity}')
        aliases.add(alias)
        textures.append({'path': '', 'hashes': {'rt64': alias, 'rice': identity}})
        records.append({'identity': identity, 'file': name, 'sha256': digest(destination),
                        'dimensions': dimensions, 'operation': operation,
                        'sources': [{'variant': variant, 'path': path.relative_to(source).as_posix(),
                                     'sha256': digest(path)} for variant, path in sorted(variants.items())]})
    write_json(output / 'rt64.json', {
        'configuration': {'configurationVersion': 3, 'autoPath': 'rice',
                          'defaultOperation': 'stream', 'defaultShift': 'none', 'hashVersion': 5},
        'textures': textures, 'operationFilters': [], 'shiftFilters': [], 'extraFiles': []})
    write_json(output / 'manifest.json', {
        'name': "DKR REMASTERED (SR.GU's)", 'author': 'SR.GU (sr.gu)',
        'sourceFolder': source.name, 'sourceTextureCount': sum(len(v) for v in groups.values()),
        'textureCount': len(records), 'mergedRgbAlphaPairs': merged,
        'coordinatePolicy': 'legacy-rice-no-shift', 'textures': records, 'excludedFiles': ignored})
    print(f'Prepared {len(records)} textures; merged {merged} RGB/alpha pairs')


def verify(output):
    manifest = json.loads((output / 'manifest.json').read_text())
    database = json.loads((output / 'rt64.json').read_text())
    expected_configuration = {'configurationVersion': 3, 'autoPath': 'rice',
                              'defaultOperation': 'stream', 'defaultShift': 'none', 'hashVersion': 5}
    if database['configuration'] != expected_configuration:
        raise ValueError('Unexpected RT64 coordinate/hash policy')
    records = manifest['textures']
    if not records or manifest['textureCount'] != len(records) or len(database['textures']) != len(records):
        raise ValueError('Texture count mismatch')
    expected = []
    names = set()
    aliases = set()
    for record in records:
        identity = record['identity']
        name = f'Diddy Kong Racing#{identity}_all.png'
        if record['file'] != name or not PATTERN.fullmatch('#' + identity + '_all.png') or name in names:
            raise ValueError(f'Invalid/duplicate identity: {identity}')
        names.add(name)
        data = (output / name).read_bytes()
        if data[:8] != b'\x89PNG\r\n\x1a\n' or hashlib.sha256(data).hexdigest() != record['sha256']:
            raise ValueError(f'Texture bytes changed: {name}')
        if list(struct.unpack('>II', data[16:24])) != record['dimensions']:
            raise ValueError(f'Texture dimensions changed: {name}')
        alias = native_alias(identity)
        if alias in aliases:
            raise ValueError(f'Native alias collision: {identity}')
        aliases.add(alias)
        expected.append({'path': '', 'hashes': {'rt64': alias, 'rice': identity}})
    if database['textures'] != expected or names != {p.name for p in output.glob('*.png')}:
        raise ValueError('RT64 database or image inventory does not match manifest')
    print(f'Verified {len(records)} bundled textures, hashes, dimensions and RT64 aliases')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, help='Supplied Rice folder; omit to verify only')
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    if args.source:
        convert(args.source, args.output)
    verify(args.output)


if __name__ == '__main__':
    main()
