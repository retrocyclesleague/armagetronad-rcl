#!/usr/bin/env python3
"""Copy the linked library closure and rewrite it to bundle-relative paths."""
import pathlib
import shutil
import subprocess
import sys

binary, destination, sdl2 = map(pathlib.Path, sys.argv[1:])
queue = [(binary, binary), (sdl2, destination / sdl2.name)]
# Current Homebrew aliases SDL2 to SDL2-compat, which dlopens SDL3 instead
# of recording it in its linked dependencies. Keep classic SDL2 unchanged.
if not sdl2.is_file():
    raise SystemExit(f'Missing SDL2 runtime: {sdl2}')
uses_sdl3 = b'libSDL3.dylib' in sdl2.read_bytes()
if uses_sdl3:
    prefix = subprocess.check_output(['brew', '--prefix', 'sdl3'], text=True).strip()
    sdl3 = pathlib.Path(prefix) / 'lib/libSDL3.dylib'
    if not sdl3.is_file():
        raise SystemExit(f'SDL2-compat requires SDL3; install Homebrew sdl3 ({sdl3})')
    # Preserve the unversioned name SDL2-compat requests via dlopen.
    queue.append((sdl3, destination / 'libSDL3.dylib'))
copied = {}
def run(*args):
    return subprocess.check_output(args, text=True)
while queue:
    source, target = queue.pop(0)
    source = source.resolve()
    if source in copied:
        existing = copied[source]
        if target != existing and not target.exists():
            target.symlink_to(existing.name)
        continue
    copied[source] = target
    if source != target.resolve():
        if target.exists():
            raise SystemExit(f'Library filename collision: {target}')
        shutil.copy2(source, target)
    target.chmod(0o755)
    if target != binary:
        subprocess.run(['install_name_tool', '-id', '@rpath/' + target.name, str(target)], check=True)
        loads = run('otool', '-l', str(target)).splitlines()
        rpaths = [loads[i + 2].strip().split('path ', 1)[1].split(' (offset', 1)[0]
                  for i, line in enumerate(loads) if line.strip() == 'cmd LC_RPATH']
        external = {p for p in rpaths if not p.startswith(('@loader_path', '@executable_path'))}
        for path in external:
            subprocess.run(['install_name_tool', '-delete_rpath', path, str(target)], check=True)
        if external and '@loader_path' not in rpaths:
            subprocess.run(['install_name_tool', '-add_rpath', '@loader_path', str(target)], check=True)
    for line in run('otool', '-L', str(source)).splitlines()[1:]:
        if line.rstrip().endswith(':'):
            continue
        dep = line.strip().split(' (', 1)[0]
        if dep.startswith(('/System/Library/', '/usr/lib/')):
            continue
        if dep.startswith('@'):
            # Bundling takes a freshly linked binary and absolute Homebrew dependencies.
            if dep in ('@rpath/' + target.name, '@rpath/' + source.name):
                continue
            raise SystemExit(f'Unresolved source dependency: {dep} in {source}')
        library = pathlib.Path(dep)
        if library.resolve() == source:
            continue
        if not library.is_file():
            raise SystemExit(f'Missing source library: {library}')
        nested = destination / library.name
        queue.append((library, nested))
        replacement = ('@executable_path/../Frameworks/' if target == binary else '@loader_path/') + nested.name
        subprocess.run(['install_name_tool', '-change', dep, replacement, str(target)], check=True)
print(f'Bundled {len(copied)-1} dynamic libraries (SDL2-compat/SDL3: {uses_sdl3})')
