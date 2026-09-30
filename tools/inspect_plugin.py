#!/usr/bin/env python3
"""Static ELF inspection for the repo's ARM32 hard-float MPC VST2 target.
Never loads a plugin or runs ldd. Requires GNU readelf from binutils.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys


def readelf(path, *flags):
    env = dict(os.environ, LC_ALL='C')
    p = subprocess.run(['readelf', *flags, '--', str(path)], capture_output=True,
                       text=True, errors='replace', timeout=30, env=env)
    if p.returncode:
        raise ValueError(p.stderr.strip() or 'readelf failed')
    return p.stdout


def version_tuple(v):
    return tuple(int(x) for x in v.split('.'))


def inspect(path, args, libraries):
    result = {'path': str(path), 'target': 'ARM32 little-endian hard-float VST2',
              'blockers': [], 'unknowns': [], 'warnings': []}
    try:
        with path.open('rb') as f:
            if f.read(4) != b'\x7fELF':
                raise ValueError('Not an ELF binary')
            f.seek(0)
            digest = hashlib.file_digest(f, 'sha256').hexdigest()
        header = readelf(path, '-hW')
        attrs = readelf(path, '-AW')
        dynamic = readelf(path, '-dW')
        symbols = readelf(path, '--dyn-syms', '-W')
        versions = readelf(path, '-VW')
        fields = {}
        for line in header.splitlines():
            if ':' in line:
                k, v = line.strip().split(':', 1)
                fields[k] = v.strip()
        exports, undefined = set(), set()
        for line in symbols.splitlines():
            parts = line.split()
            if len(parts) >= 8 and parts[0].rstrip(':').isdigit():
                name = parts[7].split('@')[0]
                if parts[6] == 'UND':
                    undefined.add(name)
                elif parts[4] in ('GLOBAL', 'WEAK') and parts[5] in ('DEFAULT', 'PROTECTED'):
                    exports.add(name)
        entrypoints = sorted(exports & {'VSTPluginMain', 'main', 'GetPluginFactory',
                                         'lv2_descriptor', 'ladspa_descriptor', 'clap_entry'})
        formats = []
        for symbol, fmt in [('VSTPluginMain', 'VST2'), ('GetPluginFactory', 'VST3'),
                            ('lv2_descriptor', 'LV2'), ('ladspa_descriptor', 'LADSPA'),
                            ('clap_entry', 'CLAP')]:
            if symbol in exports:
                formats.append(fmt)
        needed = re.findall(r'\(NEEDED\).*?\[(.*?)\]', dynamic)
        paths = re.findall(r'\((?:RUNPATH|RPATH)\).*?\[(.*?)\]', dynamic)
        # Only version requirements, not versions defined by this library.
        requirement_text = versions.split('Version needs section', 1)[-1] if 'Version needs section' in versions else ''
        requirements = sorted(set(re.findall(r'Name: ((?:GLIBC|GLIBCXX|CXXABI)_[\w.]+)', requirement_text)))
        glibc = [v[6:] for v in requirements if re.fullmatch(r'GLIBC_\d+(?:\.\d+)+', v)]
        max_glibc = max(glibc, key=version_tuple) if glibc else None
        hardfloat = 'hard-float ABI' in fields.get('Flags', '') or bool(re.search(r'Tag_ABI_VFP_args:\s*VFP registers', attrs))
        result.update(sha256=digest, elf=fields, arm_attributes=attrs.strip(),
                      detected_formats=formats, entrypoints=entrypoints,
                      direct_dependencies=needed, runtime_search_paths=paths,
                      version_requirements=requirements, max_glibc_required=max_glibc,
                      hard_float_evidence=hardfloat,
                      undefined_symbols=sorted(undefined))
        blockers = result['blockers']
        if fields.get('Class') != 'ELF32' or fields.get('Machine') != 'ARM':
            blockers.append('CPU/class differs from target: rebuild from source for ARM32')
        if 'little endian' not in fields.get('Data', ''):
            blockers.append('Target requires little-endian ELF')
        if not fields.get('Type', '').startswith('DYN'):
            blockers.append('Not an ELF shared object')
        if fields.get('Machine') == 'ARM' and not hardfloat:
            result['unknowns'].append('ARM hard-float ABI not established; inspect build flags/attributes')
        if 'soft-float ABI' in fields.get('Flags', ''):
            blockers.append('Explicit soft-float ABI differs from armhf target')
        if 'VST2' not in formats:
            if 'main' in exports:
                result['unknowns'].append('Legacy main export might be VST2; static inspection cannot confirm')
            else:
                blockers.append('No VSTPluginMain export: needs a VST2 port/adapter or further format identification')
        if max_glibc and args.glibc and version_tuple(max_glibc) > version_tuple(args.glibc):
            blockers.append(f'Requires GLIBC {max_glibc}; target specified as {args.glibc}')
        elif not args.glibc:
            result['unknowns'].append('Target GLIBC version not supplied')
        if libraries is not None:
            missing = [n for n in needed if n not in libraries]
            result['dependency_inventory_missing'] = missing
            if missing:
                blockers.append('Direct libraries absent from supplied target inventory: ' + ', '.join(missing))
            result['unknowns'].append('Inventory matches filenames only; library ABI, symbol versions and transitive resolution need verification')
        else:
            result['unknowns'].append('Target library inventory not supplied; dependency availability unknown')
        gui = [n for n in needed if re.search(r'X11|xcb|GL\.|gtk|Qt|wayland', n, re.I)]
        if gui:
            result['warnings'].append('Desktop GUI dependencies: ' + ', '.join(gui) + '; headless loading/native skin requires testing')
        result['unknowns'] += ['Source availability and distribution license need upstream review',
                              'CPU instruction requirements, loadability, MIDI/audio, parameter mapping, chunks and CPU budget require tests']
        result['verdict'] = 'REBUILD_OR_ADAPT_REQUIRED' if blockers else 'CANDIDATE_REQUIRES_VERIFICATION'
    except (OSError, ValueError, subprocess.TimeoutExpired) as exc:
        result['verdict'] = 'INSPECTION_ERROR'
        result['error'] = str(exc)
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('paths', nargs='+', type=Path, help='Binary files or directories (recursive .so/.clap scan)')
    p.add_argument('--glibc', help='Actual target GLIBC version, for example 2.39')
    p.add_argument('--target-libs', type=Path, help='Text file listing target library basenames or absolute paths, one per line')
    p.add_argument('--json', type=Path, help='Write machine-readable report')
    args = p.parse_args()
    if not shutil.which('readelf'):
        p.error('GNU readelf is required: install binutils on Linux/WSL')
    if args.glibc and not re.fullmatch(r'\d+(?:\.\d+)+', args.glibc):
        p.error('--glibc must be a numeric version')
    libraries = None
    if args.target_libs:
        try:
            libraries = {Path(x.strip()).name for x in args.target_libs.read_text().splitlines() if x.strip()}
        except OSError as exc:
            p.error(str(exc))
    paths = set()
    for path in args.paths:
        if path.is_dir():
            paths.update(x.resolve() for x in path.rglob('*') if x.is_file() and
                         (x.name.endswith('.clap') or re.search(r'\.so(?:\.|$)', x.name)))
        else:
            paths.add(path.resolve())
    if not paths:
        p.error('No .so/.clap files found; unpack downloads before scanning')
    reports = [inspect(path, args, libraries) for path in sorted(paths)]
    for r in reports:
        print(f"{r['verdict']}: {r['path']}")
        print('  Formats:', ', '.join(r.get('detected_formats', [])) or 'unknown')
        for kind in ('blockers', 'warnings', 'unknowns'):
            for message in r[kind]:
                print(f'  {kind}: {message}')
        if 'error' in r:
            print('  error:', r['error'])
    if args.json:
        args.json.write_text(json.dumps({'schema_version': 1, 'plugins': reports}, indent=2) + '\n')
    return 2 if any(r['verdict'] == 'INSPECTION_ERROR' for r in reports) else (1 if any(r['blockers'] for r in reports) else 0)

if __name__ == '__main__':
    sys.exit(main())
