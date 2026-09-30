#!/usr/bin/env python3
"""Create a self-contained sine instrument scaffold using the shared MPC tools."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil

KIT = Path(__file__).resolve().parents[1]
ENGINE = r'''/* Original test DSP: replace with the intended engine before release. */
#include "engine.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct { double phase, hz; float gain, velocity; int note, active; } synth_t;
static void *create(const char *dir) {
    (void)dir;
    synth_t *s = calloc(1, sizeof *s);
    if (s) { s->gain = 0.2f; s->note = -1; }
    return s;
}
static void destroy(void *p) { free(p); }
static void midi(void *p, const uint8_t *m, int n) {
    synth_t *s = p;
    if (!s || !m || n < 3) return;
    int op = m[0] & 0xf0;
    if (op == 0x90 && m[2]) {
        s->note = m[1] & 127;
        s->hz = 440.0 * pow(2.0, (s->note - 69) / 12.0);
        s->velocity = (m[2] & 127) / 127.0f; s->active = 1;
    } else if (((op == 0x80 || op == 0x90) && m[1] == s->note) ||
               (op == 0xb0 && (m[1] == 120 || m[1] == 123))) s->active = 0;
}
static void set_param(void *p, const char *key, const char *value) {
    synth_t *s = p; float v; char tail;
    if (!s || !key || !value) return;
    if (!strcmp(key, "gain")) {
        if (sscanf(value, "%f %c", &v, &tail) != 1) return;
    } else if (!strcmp(key, "state")) {
        if (sscanf(value, "v1 gain=%f %c", &v, &tail) != 1) return;
    } else return;
    if (isfinite(v) && v >= 0 && v <= 1) s->gain = v;
}
static int get_param(void *p, const char *key, char *buf, int n) {
    synth_t *s = p;
    if (!s || !key || !buf || n <= 0) return 0;
    if (!strcmp(key, "gain")) return snprintf(buf, n, "%.9g", s->gain);
    if (!strcmp(key, "state")) return snprintf(buf, n, "v1 gain=%.9g", s->gain);
    buf[0] = 0; return 0;
}
static void render(void *p, int16_t *out, int frames) {
    synth_t *s = p;
    for (int i = 0; i < frames; ++i) {
        int16_t v = 0;
        if (s && s->active) {
            v = (int16_t)(sin(s->phase) * s->gain * s->velocity * 32767.0);
            s->phase += 6.283185307179586 * s->hz / 44100.0;
            if (s->phase >= 6.283185307179586) s->phase -= 6.283185307179586;
        }
        out[2*i] = out[2*i+1] = v;
    }
}
static const mpc_engine_t api = {create, destroy, midi, set_param, get_param, render, NULL};
const mpc_engine_t *mpc_engine(void) { return &api; }
'''

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('destination', type=Path)
    ap.add_argument('--name', required=True)
    ap.add_argument('--vendor', required=True)
    ap.add_argument('--uid', required=True)
    args = ap.parse_args()
    if not re.fullmatch(r'[A-Za-z0-9]{4}', args.uid):
        ap.error('--uid must be four ASCII letters/digits')
    for field in ('name', 'vendor'):
        value = getattr(args, field)
        if not value.strip() or any(c in value for c in '/\\<>"&') or any(ord(c) < 32 for c in value):
            ap.error(f'--{field} must be non-empty and safe for XML and folder names')
    slug = re.sub(r'[^a-z0-9]+', '-', args.name.lower()).strip('-')
    if not slug:
        ap.error('--name needs at least one ASCII letter/digit')
    dest = args.destination.resolve()
    if dest.exists():
        ap.error('destination already exists; no files were overwritten')
    so = slug.replace('-', '_') + '.so'
    for manifest in (KIT / 'ports').glob('*/vst.json'):
        cfg = json.loads(manifest.read_text())
        if cfg.get('uid') == args.uid or cfg.get('so') == so:
            ap.error(f'UID or library name collides with {manifest}')
    include = Path(os.path.relpath(KIT / 'wrapper', dest)).as_posix()
    cfg = {'name': args.name, 'vendor': args.vendor, 'uid': args.uid,
           'version': 1000, 'so': so, 'params': 'params.json',
           'build': {'root': '.', 'sources': ['src/engine.c'],
                     'cflags': ['-I' + include], 'libs': ['-lm']}}
    dest.mkdir(parents=True)
    (dest / 'src').mkdir()
    (dest / 'src/engine.c').write_text(ENGINE)
    (dest / 'vst.json').write_text(json.dumps(cfg, indent=2) + '\n')
    (dest / 'params.json').write_text(json.dumps({'name': args.name, 'params': [
        {'key': 'gain', 'name': 'Gain', 'min': 0, 'max': 1, 'default': 0.2}],
        'sections': [{'label': 'Performance', 'keys': ['gain']}]}, indent=2) + '\n')
    for name in ('BRIEF.md', 'STATUS.md'):
        shutil.copyfile(KIT / 'templates/plugin' / name, dest / name)
    shutil.copyfile(KIT / 'templates/plugin/VENDORED.md', dest / 'src/VENDORED.md')
    tool = Path(os.path.relpath(KIT / 'tools', dest)).as_posix()
    (dest / 'README.md').write_text(f'''# {args.name} — MPC instrument scaffold

Original monophonic sine test engine. Gain, velocity, matching note-off, CC120/123
and versioned gain state are implemented. Replace the DSP and complete BRIEF.md.

From this folder (quote paths if your checkout contains spaces):

```sh
bash "{tool}/test_port.sh" vst.json
bash "{tool}/build_port.sh" vst.json
```

The skin initially uses auto-layout. See the shared AGENTS.md and
AGENT_WORKFLOW.md for skin design, validation and distribution gates.
STATUS.md records unfinished work. No ARM/device verification or distribution
license is implied by generating this scaffold.
''')
    print(f'Created {dest}; next: fill BRIEF.md and run the offline host test.')

if __name__ == '__main__':
    main()
