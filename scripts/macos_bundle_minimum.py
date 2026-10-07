#!/usr/bin/env python3
"""Report the highest macOS deployment target required by bundled native code."""
import pathlib
import re
import subprocess
import sys


def version_tuple(version):
    parts = tuple(map(int, version.split('.')))
    return parts + (0,) * (3 - len(parts))


def minimum_version(contents):
    versions = []
    for code in pathlib.Path(contents).rglob('*'):
        if code.is_symlink() or not code.is_file():
            continue
        if 'Mach-O' not in subprocess.check_output(['file', '-b', str(code)], text=True):
            continue
        loads = subprocess.check_output(['otool', '-l', str(code)], text=True)
        declared = []
        for block in re.split(r'Load command \d+\n', loads):
            if re.search(r'^\s*cmd LC_BUILD_VERSION$', block, re.M):
                if not re.search(r'^\s*platform (?:1|macos)$', block, re.M):
                    raise ValueError(f'Non-macOS native code: {code}')
                declared += re.findall(r'^\s*minos ([0-9]+(?:\.[0-9]+){0,2})$', block, re.M)
            elif re.search(r'^\s*cmd LC_VERSION_MIN_MACOSX$', block, re.M):
                declared += re.findall(r'^\s*version ([0-9]+(?:\.[0-9]+){0,2})$', block, re.M)
        if not declared:
            raise ValueError(f'Missing macOS deployment target: {code}')
        versions.extend(declared)
    if not versions:
        raise ValueError('No native code in bundle')
    return max(versions, key=version_tuple)


if __name__ == '__main__':
    print(minimum_version(sys.argv[1]))
