#!/usr/bin/env bash
# A MIDI-generator wrapper: reuse the repository generator/skin tools, not the DSP-only wrapper.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
P=ports/quadweave
if [ "${1:-}" = --host ]; then
    cd "$ROOT"
    mkdir -p "$P/build"
    gcc -O2 -Itools/vendor/force-shadow/tools tools/shadow_art.c -lm -o "$P/build/shadow_art"
    python3 tools/gen_vst.py "$P/vst.json"
    g++ -std=c++17 -O2 -Wall -Wextra -Werror -fPIC -shared -fvisibility=hidden \
        -I"$P/build" -Iwrapper "$P"/src/{midi,progression,player,plugin}.cpp \
        -lasound -pthread -ldl -Wl,--no-undefined -Wl,--version-script="$P/exports.map" \
        -o "$P/build/quadweave-host.so"
    exit 0
fi
if [ -n "${1:-}" ]; then echo 'Usage: build.sh [--host]' >&2; exit 2; fi
command -v docker >/dev/null || { echo 'ARM build requires Docker with ARMv7 emulation.' >&2; exit 1; }
IMAGE=${QUADWEAVE_ARM_IMAGE:-quadweave-arm32:gcc12}
docker build --platform linux/arm/v7 -f "$ROOT/$P/Dockerfile.arm32" -t "$IMAGE" "$ROOT/$P"
docker run --rm --platform linux/arm/v7 -u "$(id -u):$(id -g)" -v "$ROOT":/work -w /work "$IMAGE" bash -euc '
P=ports/quadweave
mkdir -p "$P/build"
gcc -O2 -Itools/vendor/force-shadow/tools tools/shadow_art.c -lm -o "$P/build/shadow_art"
python3 tools/gen_vst.py "$P/vst.json"
g++ -std=c++17 -O2 -Wall -Wextra -Werror -fPIC -shared -fvisibility=hidden \
    -I"$P/build" -Iwrapper "$P"/src/{midi,progression,player,plugin}.cpp \
    -lasound -pthread -ldl -Wl,--no-undefined -Wl,--version-script="$P/exports.map" -o "$P/build/quadweave.so"
strip "$P/build/quadweave.so"
readelf -h "$P/build/quadweave.so"
readelf --dyn-syms -W "$P/build/quadweave.so" | grep VSTPluginMain
readelf -V "$P/build/quadweave.so" | grep -o "GLIBC_[0-9.]*" | sort -Vu | tail -1
'
