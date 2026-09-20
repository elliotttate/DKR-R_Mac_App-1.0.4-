#!/usr/bin/env python3
"""Reject bundles whose native code or embedded Metal shaders exceed their OS floor."""

import argparse
import json
from pathlib import Path
import plistlib
import re
import subprocess


METAL_TARGET = re.compile(rb'air64(?:_v\d+)?-apple-macosx(\d+\.\d+(?:\.\d+)?)')
MACH_MAGICS = {bytes.fromhex(x) for x in (
    'cffaedfe', 'cefaedfe', 'feedfacf', 'feedface', 'cafebabe', 'bebafeca',
    'cafebabf', 'bfbafeca')}
USAGE_KEYS = (
    'NSDocumentsFolderUsageDescription', 'NSDownloadsFolderUsageDescription',
    'NSDesktopFolderUsageDescription', 'NSRemovableVolumesUsageDescription',
    'NSNetworkVolumesUsageDescription', 'NSFileProviderDomainUsageDescription',
    'NSLocalNetworkUsageDescription')


def version(value):
    parts = tuple(int(part) for part in value.split('.'))
    return parts + (0,) * (3 - len(parts))


def require(condition, message):
    if not condition:
        raise ValueError(message)


def check_metal(data, label, target):
    targets = {match.decode() for match in METAL_TARGET.findall(data)}
    for found in targets:
        require(version(found) <= target,
                f'{label}: Metal targets macOS {found}, newer than the supported OS')
    return sorted(targets)


def validate(app, shader_directory, minimum):
    target = version(minimum)
    require(target >= version('12.0'), 'DKR-R requires macOS 12.0 or newer')
    info = plistlib.loads((app / 'Contents/Info.plist').read_bytes())
    require(version(info.get('LSMinimumSystemVersion', '0.0')) == target,
            'Info.plist must declare the same minimum macOS version as the build')
    for key in USAGE_KEYS:
        require(bool(info.get(key, '').strip()), f'Missing permission explanation: {key}')

    executable = app / 'Contents/MacOS' / info['CFBundleExecutable']
    game_bytes = executable.read_bytes()
    libraries = sorted(shader_directory.rglob('*.metallib'))
    require(bool(libraries), 'No compiled Metal libraries found for verification')
    embedded = 0
    for library in libraries:
        data = library.read_bytes()
        targets = check_metal(data, str(library.name), target)
        require(bool(targets), f'{library.name}: could not verify the Metal deployment target')
        if data in game_bytes:
            embedded += 1
    require(embedded > 0, 'The packaged executable does not contain the checked game shaders')

    binaries = []
    for path in sorted(app.rglob('*')):
        if not path.is_file() or path.is_symlink():
            continue
        with path.open('rb') as stream:
            magic = stream.read(4)
        if magic not in MACH_MAGICS:
            continue
        relative = str(path.relative_to(app))
        output = subprocess.check_output(['otool', '-l', str(path)], text=True)
        versions = re.findall(r'\bminos\s+(\d+(?:\.\d+)+)', output)
        versions += re.findall(r'LC_VERSION_MIN_MACOSX\s+cmdsize\s+\d+\s+version\s+(\d+(?:\.\d+)+)', output)
        require(bool(versions), f'{relative}: missing Mach-O deployment target')
        for found in versions:
            require(version(found) <= target, f'{relative}: native code requires macOS {found}')
        dependencies = subprocess.check_output(['otool', '-L', str(path)], text=True)
        for line in dependencies.splitlines():
            if line.startswith('\t'):
                dependency = line.strip().split(' (', 1)[0]
                require(dependency.startswith(('@', '/System/Library/', '/usr/lib/')),
                        f'{relative}: unbundled dependency {dependency}')
        shader_targets = check_metal(path.read_bytes(), relative, target)
        binaries.append({'path': relative, 'minimum_macos': versions,
                         'metal_targets': shader_targets})
    require(bool(binaries), 'No native binaries checked')
    return {'minimum_macos': minimum, 'compiled_metal_libraries': len(libraries),
            'embedded_game_metal_libraries': embedded, 'permission_keys': list(USAGE_KEYS),
            'binaries': binaries,
            'scope': 'Build compatibility audit; execution on each supported macOS still requires native QA.'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app', type=Path, required=True)
    parser.add_argument('--shaders', type=Path, required=True)
    parser.add_argument('--minimum', required=True)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    report = validate(args.app, args.shaders, args.minimum)
    if args.report:
        args.report.write_text(json.dumps(report, indent=2) + '\n')
    print(f"macOS {args.minimum} audit passed: {len(report['binaries'])} native binaries, "
          f"{report['compiled_metal_libraries']} Metal libraries, "
          f"{report['embedded_game_metal_libraries']} embedded in the game; permission messages present.")


if __name__ == '__main__':
    main()
