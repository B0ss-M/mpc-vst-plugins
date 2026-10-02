#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
P=ports/quadweave
mkdir -p "$P/build"
python3 tools/gen_vst.py "$P/vst.json" --params-h
CXX=${CXX:-g++}
FLAGS=(-std=c++17 -g -O1 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -I"$P/src" -I"$P/build" -Iwrapper -pthread)
"$CXX" "${FLAGS[@]}" "$P"/src/{midi,progression,player}.cpp "$P/tests/test_core.cpp" -o "$P/build/test_core"
"$P/build/test_core"
"$CXX" "${FLAGS[@]}" -DQW_NO_ALSA "$P"/src/{midi,progression,player,plugin}.cpp "$P/tests/test_host.cpp" -ldl -o "$P/build/test_host"
"$P/build/test_host"
