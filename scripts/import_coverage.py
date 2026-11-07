"""Static import coverage of a game folder: which imports the runtime binds, without running anything.

An unbound import stops the game only when it is called, so this lists what can stop it, not what will. Bindings are read
from the sources: "NID#library" literals, the RuntimeExport name tables and the video core's LIB_FUNCTION registrations.
The eboot's imports of a bundled module bind to that module's exports.

Usage: python scripts/import_coverage.py <game dir> [--eboot-only] [--json FILE]
"""
import argparse
import collections
import json
from pathlib import Path
import re
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import game_profiles  # noqa: E402
from ps4_module import read_module  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
LITERAL = re.compile(r'"([A-Za-z0-9+\-]{11})#(lib\w+)"')
TABLE = re.compile(r'\{\s*"([A-Za-z_]\w*)"\s*,\s*[A-Za-z_(&]')
GPU = re.compile(r'LIB_FUNCTION\(\s*"([A-Za-z0-9+\-]{11})"')
NAME = re.compile(r'\{"([^"#]+)#(\w+)","([^"]+)"\}')
RUNTIME_SOURCES = ('src/*.c', 'src/*.cpp', 'src/trophies/*.cpp')
GPU_SOURCES = ('gpu/shim/*.cpp', 'gpu/shim/**/*.cpp', 'gpu/shadps4/core/libraries/videodec/*.cpp', 'gpu/shadps4/core/libraries/gnmdriver/*.cpp',
               'gpu/shadps4/core/libraries/videoout/*.cpp', 'gpu/shadps4/core/libraries/kernel/*.cpp')


def runtime_bindings():
    literals, tables, gpu = set(), set(), set()
    for pattern in RUNTIME_SOURCES:
        for path in ROOT.glob(pattern):
            text = path.read_text(errors='replace')
            literals.update(LITERAL.findall(text))
            tables.update(TABLE.findall(text))
    for pattern in GPU_SOURCES:
        for path in ROOT.glob(pattern):
            gpu.update(GPU.findall(path.read_text(errors='replace')))
    names = {(n, lib): symbol for n, lib, symbol in NAME.findall((ROOT / 'src' / 'import_names.inc').read_text())}
    return literals, tables, gpu, names


def module_imports(path):
    module = read_module(path)
    imports = {(s['identity'][0], 'libkernel' if s['identity'][1][0] == 'libScePosix' else s['identity'][1][0])
               for s in module['symbols'] if s['identity'] and not s['section']}
    exports = {s['identity'][0] for s in module['symbols'] if s['identity'] and s['section']}
    return imports, exports


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('game', type=Path)
    parser.add_argument('--eboot-only', action='store_true', help='leave out what only the bundled modules import')
    parser.add_argument('--json', type=Path)
    args = parser.parse_args()
    profile = game_profiles.profile(args.game)
    if not profile:
        sys.exit(f'{args.game}: not a supported game folder')

    eboot, _ = module_imports(args.game / profile['entry'])
    wanted, exported = set(eboot), set()
    for name in profile['modules']:
        imports, exports = module_imports(args.game / 'sce_module' / name)
        exported |= exports
        if not args.eboot_only:
            wanted |= imports

    literals, tables, gpu, names = runtime_bindings()
    bound, native, missing = 0, 0, collections.defaultdict(list)
    for nid, library in sorted(wanted):
        symbol = names.get((nid, library))
        if (nid, library) in literals or nid in gpu or symbol in tables:
            bound += 1
        elif nid in exported:
            native += 1
        else:
            missing[library].append(symbol or nid)
    total_missing = sum(map(len, missing.values()))
    print(f'{args.game}: {len(wanted)} imports: {bound} runtime, {native} bundled modules, {total_missing} unbound')
    for library, symbols in sorted(missing.items(), key=lambda item: (-len(item[1]), item[0])):
        print(f'  {library}: {len(symbols)}')
        for symbol in sorted(symbols):
            print(f'    {symbol}')
    if args.json:
        args.json.write_text(json.dumps({'total': len(wanted), 'runtime': bound, 'bundled': native,
                                         'unbound': {k: sorted(v) for k, v in missing.items()}}, indent=2) + '\n')
    return 0


if __name__ == '__main__':
    sys.exit(main())
