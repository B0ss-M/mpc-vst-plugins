# QUADWEAVE status

Status: implemented and host verified; ARM/device gates outstanding.
Updated: 2026-10-02.

## Reproducible checkpoint

- Base source: `def23bc3a376cd4d6b7d230f4946356546cdbbf4` in B0ss-M/mpc-vst-plugins.
- Implementation: `ports/quadweave/src/`; native controls generated from `make_controls.py`.
- Host: Ubuntu 24.04 x86-64, GCC/G++ with C++17, Python 3 / Pillow.
- ARM build recipe: `ports/quadweave/build.sh`, Docker `arm32v7/gcc:12` plus libasound2-dev.
- Hardware: user-reported MPC Live 2 firmware 3.9.1; exact community-mod revision and SSH access unconfirmed.
- User MIDI files are supplied locally, never committed.

## Verification evidence

| Gate | Command / procedure | Outcome |
|---|---|---|
| Parser / player / browser core | `ASAN_OPTIONS=detect_leaks=0 ports/quadweave/test.sh` | PASS: SMF running status/sustain/malformed data, progression JSON, four speeds, 5:7, reverse/ping-pong, transforms, note ownership, state, browser confinement, SPSC queue |
| VST host lifecycle | Same test script, test_host | PASS: asynchronous progression load, host timing, silent output, chunks, failed restore, two instances, legacy process, shutdown |
| Address / undefined behaviour sanitizers | Same test script | PASS with ASan/UBSan. LeakSanitizer could not run: environment denied `/proc/.../task`; explicit detect_leaks=0 override used only for local tests. CI retains default leak checking. |
| Existing DSP-wrapper regression | `tools/new_port.py` temporary fixture, then `tools/test_port.sh` | PASS: generic starter still plays audio, restores chunks and isolates instances after shared ABI extraction |
| Native ALSA link | x86-64 build with alsa-lib v1.2.8 headers and system libasound.so.2, `--no-undefined` | PASS: real ALSA linkage compiles. This is not an ALSA hardware playback test. |
| Skin / Q-Links | `gen_vst.py`, `studio.py preview`; inspect six page PNGs | PASS offline: four track pages, browser, status. Fixed clipped progression-duration control. Live values/viewport require hardware. |
| ARM ELF / symbol / dependency inspection | `ports/quadweave/build.sh` | NOT RUN: no Docker or ARM cross-compiler in authoring environment. Script provided. |
| Live 2 routing / timing / CPU | Four MPC destinations and recorded MIDI comparison | NOT RUN: no device connection. |
| Install / restart / release / catalog | Repository deployment/release workflow | NOT RUN. No device writes, restart or release performed. |

## Next gate

1. On the user's Intel Mac, build with Docker: `ports/quadweave/build.sh`.
2. Verify ELF32 ARM hard-float, VSTPluginMain-only exports and target-compatible GLIBC/GLIBCXX/ALSA dependencies. Record image digest.
3. Establish authorized device access and back up settings. Ask before stopping/restarting MPC per AGENTS.md.
4. Verify plugin insert, callback continuity, ALSA source discovery, independent output channels, simultaneous destination monitoring/recording, Q-Links and save/reload.
5. Record actual jitter and CPU under four dense polyphonic clips; test stop/seek/mute/output changes and removal for hanging notes.

The wider design's generated arp/melody editor, Euclidean rhythms, scenes, smart metadata/favourites and export remain future work. Implemented rhythmic playback must not be described as sample-accurate delivery or as hardware verified.
