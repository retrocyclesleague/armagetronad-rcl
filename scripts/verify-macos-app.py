#!/usr/bin/env python3
"""Fail closed on missing resources, architecture slices, or external dependencies."""
import pathlib
import plistlib
import subprocess
import sys
from macos_bundle_minimum import minimum_version, version_tuple

app = pathlib.Path(sys.argv[1]).resolve()
contents = app / 'Contents'
frameworks = contents / 'Frameworks'
binary = contents / 'MacOS/armagetronad'
def output(*args):
    return subprocess.check_output(args, text=True)

with (contents / 'Info.plist').open('rb') as stream:
    info = plistlib.load(stream)
assert (contents / 'MacOS' / info['CFBundleExecutable']).is_file(), 'Missing bundle launcher'
required_macos = minimum_version(contents)
assert version_tuple(info.get('LSMinimumSystemVersion', '0')) >= version_tuple(required_macos), \
    f'Bundle minimum macOS version must be at least {required_macos}'
required = ['config/default.cfg', 'language/languages.txt', 'textures/font.png',
            'models/cycle_body.mod', 'resource/included/map.dtd', 'sound/cyclrun.wav',
            'replays/menu_fort.rclreplay', 'textures/ui/space-grotesk.fnt']
for relative in required:
    assert (contents / 'Resources' / relative).is_file(), f'Missing resource: {relative}'
architectures = set(output('lipo', '-archs', str(binary)).split())
count = 0
for code in contents.rglob('*'):
    if code.is_symlink() or not code.is_file():
        continue
    if 'Mach-O' not in output('file', '-b', str(code)):
        continue
    count += 1
    slices = set(output('lipo', '-archs', str(code)).split())
    assert architectures <= slices, f'Missing {architectures - slices} in {code}'
    lines = output('otool', '-l', str(code)).splitlines()
    rpaths = []
    for i, line in enumerate(lines):
        if line.strip() == 'cmd LC_RPATH':
            path = lines[i + 2].strip().split('path ', 1)[1].split(' (offset', 1)[0]
            assert path.startswith(('@loader_path', '@executable_path')), f'External rpath: {path}'
            expanded = path.replace('@loader_path', str(code.parent)).replace('@executable_path', str(binary.parent))
            rpaths.append(pathlib.Path(expanded))
    for line in output('otool', '-L', str(code)).splitlines()[1:]:
        if line.rstrip().endswith(':'):
            continue
        dep = line.strip().split(' (', 1)[0]
        if not dep or dep.endswith(':'):  # universal arch header
            continue
        if dep.startswith(('/System/Library/', '/usr/lib/')):
            continue
        if dep.startswith('@rpath/'):
            relative = dep.removeprefix('@rpath/')
            candidates = [root / relative for root in [frameworks, *rpaths]]
            target = next((p for p in candidates if p.exists()), candidates[0])
        elif dep.startswith('@loader_path/'):
            target = code.parent / dep.removeprefix('@loader_path/')
        elif dep.startswith('@executable_path/'):
            target = binary.parent / dep.removeprefix('@executable_path/')
        else:
            raise AssertionError(f'External dependency in {code}: {dep}')
        assert target.exists(), f'Missing bundled dependency: {dep} in {code}'

print(f'Bundle verified: {count} Mach-O files; architectures {", ".join(sorted(architectures))}; resources and dependencies present')
