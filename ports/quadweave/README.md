# QUADWEAVE — polyrhythmic MIDI player

An initial implementation for the MPC-MOD four-track musical tool. **ARM32 test build available; not a device-verified release.** This port uses a MIDI-specific VST2 wrapper and the repository's shared ABI, parameter generator, skin renderer and Q-Link mapping. Do not build it through the DSP-only `tools/build_port.sh`; use the commands below.

## Implemented

- Four independent polyphonic clip players, each with an output channel (1–16).
- Host-beat timing: 1/4×, 1/2×, 3/4×, 1×, 3/2×, 2×, 3×, 4× and 4:3, 5:4, 7:4, 2:3, 4:5, 4:7 speed ratios.
- Forward, Reverse and Ping-pong (alternate complete forward/reverse cycles). Reverse mirrors note intervals, preserving durations and simultaneous chord notes.
- MIDI format 0/1 with PPQ timing, running status and polyphony. Sustain is baked into note durations during import, so reverse playback has meaningful note lengths. Other controllers/program changes/SysEx are filtered.
- MPC `.progression` JSON with `progression.chords[].notes` arrays. Chords become a sequence in file order; empty slots become rests. Choose **Chord Beats on Load** before loading: 1/16–8 quarter-note beats per chord, 90% gate. These are assigned timings, not timing recovered from the file.
- Browser with folders, eight visible result slots, index selection, load destination, source file-track/channel filters, preview, restore previous clip and refresh. Extensions are case-insensitive.
- Input-note transposition with anchor, input channel filter, last-note priority and latch; input note changes affect future attacks, not existing note-off identities.
- Transpose, Fit Scale and Degree Map. Major, natural minor, Dorian, Mixolydian, major/minor pentatonic and chromatic. Degree Map requires equal scale cardinality; an incompatible pair produces no notes.
- Embedded clip data and lane settings in versioned VST chunks. Project recall does not depend on removable media being present.
- Separate playback and file workers. Audio processing queues host time/MIDI without filesystem, ALSA, mutex or allocation work. A per-instance ALSA output port uses four channels.

## Musical example

Use a one-beat clip containing one attack. Load it on A and B, set A to **5:4**, B to **7:4**, and route to channels 1 and 2. Over four host quarter-note beats, they produce five and seven attacks. Both remain related to host tempo. With richer phrases the same ratios stretch the whole phrase, including note durations. Different loop lengths produce polymeter as well.

The current output is **block-timed MIDI**, not sample-accurate MIDI. Events collected in one host block are delivered immediately by the worker. At 128 samples / 44.1 kHz the block is about 2.9 ms; worker/routing jitter is additional and must be measured on Live 2. A backlog resets notes rather than replaying a large stale burst.

## Build and test

From repository root, on Linux with GCC/G++ (C++17) and Python/Pillow:

```sh
ports/quadweave/test.sh
```

The tests use an explicit non-ALSA host sink; no device is needed. For a native Linux `.so` with real ALSA linkage, install `libasound2-dev` and run:

```sh
ports/quadweave/build.sh --host
```

For the intended **ARM32** build from an Intel Mac with Docker and ARMv7 emulation:

```sh
ports/quadweave/build.sh
```

The ARM script builds `quadweave-arm32:gcc12` from `arm32v7/debian:bookworm-slim` with GCC 12, installs build dependencies in that image, builds the native skin and links `quadweave.so` against ALSA. It prints the ELF header, entry-point symbol and required glibc versions. ARM compilation passed in GitHub Actions run 36995026226 on 2026-10-02. The binary is ELF32 ARMv7 hard-float, exports only VSTPluginMain and requires GLIBC through 2.34 and GLIBCXX through 3.4.29. The image tag is not a pinned digest; record the resolved image before publishing reproducible releases.

Output: `ports/quadweave/build/quadweave.so`, `skin/`, `params.h`, `pluginlist-entry.xml`. **`quadweave-host.so` is x86-64 and must not be installed on MPC.**

Regenerate checked-in controls if editing the UI schema:

```sh
python3 ports/quadweave/make_controls.py
```

The generated manifest is for the repository tools; the custom build script supplies the MIDI wrapper. The prototype UID is `QdW1`, locally checked for collisions; check external ports before release.

## Browser setup and playback

1. Put your own `.mid`, `.midi` and `.progression` files in a folder named `quadweave-midi` beside the installed `.so`. Subfolders are supported. Files are never written by the browser. A host development process may override this root with `QUADWEAVE_MIDI_ROOT`.
2. In Browser choose **File Index**. The selected result has `>` and its zero-based index. Turn the index to page through results; use **Folder** to enter a directory and **Up** to leave it.
3. Choose **Load Track** A–D. **File Track** 0 means all, otherwise 1 is the first SMF track; **File Ch** 0 means all, otherwise 1–16. SMF tracks and MIDI channels are different. All intentionally merges selected parts. Progression files are one source part/channel.
4. For progression files, set **Chord Beats on Load** on Status before pressing Load. For MIDI files, original PPQ musical timing is preserved; embedded tempo maps do not override host tempo.
5. Load, then set the corresponding Track page's speed, direction, output channel, source/target key and mapping. Source key is user-selected; automatic key estimation is not implemented.
6. Start the MPC transport. Preview also requires running host transport and an enabled destination lane. It temporarily replaces only that lane's clip; **End Preview** restores the original clip at its start. Saving during preview saves the original clip.
7. Set **Input Root** On to transpose from incoming pads/keyboard notes. Anchor defaults to MIDI note 60. The control notes are consumed rather than echoed.

The plugin creates `QUADWEAVE / MIDI Out`. The Status output readout includes its actual ALSA client/port ID. Select that port/channel on the receiving MPC tracks and avoid feedback into the generator. Four-track monitoring/recording behaviour and selection persistence need Live 2 validation.

Mute/output-route/direction/loop changes release notes and restart the affected clip. Speed changes retain phase. Stop, transport discontinuity and callback loss release notes; resume/seek restarts clips at their beginning (no MIDI chase). Loop tails are cut. Same-output-note collisions are coalesced until their last owner releases; they are not retriggered separately. Use different output channels for independent articulation.

## Explicit limits / remaining work

- ARM build/inspection passed; no MPC hardware pass yet. Force routing evidence does not prove Live 2 compatibility. No installer, restart or firmware modification was performed.
- This implementation covers file/progression playback. The broader design's Euclidean/stage editor, generated arp/melody engine, scenes, favourites, search, automatic key estimation, MIDI export and chord-aware revoicing are not implemented here.
- Unsupported MIDI format 2 and SMPTE files fail clearly. Malformed files and unmatched releases fail rather than silently repair.
- Caps: 8 MiB MIDI, 100,000 parsed MIDI events, 64 source tracks, 32 simultaneous notes per lane, 1 MiB progression JSON, 128 chord slots, 4,096 folder entries. Symbolic links are omitted. Partial loads leave the previous clip intact.
- The offline skin renderer cannot display live file names/value strings. Live 2 viewport, labels, Q-Link order and touch behaviour still require device testing.
- Project chunk compatibility is versioned; this is an unreleased schema. Do not reorder released parameters later.

Read `STATUS.md` for test evidence and the next device gate. Follow repository `AGENTS.md` before deployment, including backups and permission before restarting MPC.
