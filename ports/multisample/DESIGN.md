# Multisample instrument for MPC OS — design draft

Status: design only. No engine, importer, device build, or device test exists yet.

## Goal and boundary

A playable VST2 instrument for MPC Live/One/X/Key and Force with native touchscreen and Q-Link pages. A desktop import step reads multisample instruments in several source formats and makes a portable instrument bundle. The ARM plugin loads that bundle and plays it from pads, keys, MIDI clips, and the sequencer. Importing a source does not imply every feature of the source instrument can be reproduced; the importer must report each unsupported or approximated feature.

ConvertWithMoss is the **desktop import reference and optional Java dependency**, not the audio engine. Its `IMultisampleSource` / `IGroup` / `ISampleZone` model carries the mapping. Its readers cover SFZ, SoundFont 2, Kontakt, EXS24, KMP, modern MPC, and individual WAV/AIFF/CAF/FLAC/NCW/OGG files, among many others. Reader support and preservation of a particular feature vary by format; import tests must establish that per source. Pin the exact ConvertWithMoss commit in the importer build. If its LGPLv3 code is linked or redistributed, follow its license obligations and provide the required notices/source access. The plugin itself contains no ConvertWithMoss code or Java runtime.

The format-specific desktop interface and MPC presentation are specified in [FORMAT_UI.md](FORMAT_UI.md). The [interactive design preview](ui/format-import.html) demonstrates the import flow; it does not parse files yet.

## Two-stage architecture

1. **Desktop importer (Java, outside the VST):** detect one source instrument using ConvertWithMoss; flatten inherited group settings exactly once; extract/decode samples; write mono/stereo little-endian PCM WAV plus a versioned `instrument.json`; validate all ranges and report lost features. Start with SFZ and SoundFont 2, then add Kontakt, EXS24, KMP and MPC XPM fixtures. An individual audio file can form a one-zone instrument, with an explicitly selected root note. The importer can run on macOS, Windows or Linux; conversion need not run on the MPC.
2. **Device engine (C++17, VST2 via `wrapper/vst2_wrap.c`):** load a bundle from `/sdcard/Multisamples/<bank>/<instrument>/`; index its immutable zones off the audio thread; decode/cache PCM before activating a patch; select zones on note-on and mix voices in the host's 44.1 kHz, 128-frame blocks. The audio callback does no filesystem I/O, allocation, locks, format parsing, or waiting. A patch switch loads on a worker, then atomically swaps an immutable snapshot at a block boundary; existing voices retain the old snapshot until their release ends. Show a load error while keeping the previous patch playable.

No arbitrary filesystem paths enter the audio engine from a preset. All referenced files must stay inside the bundle after canonical path resolution. Apply caps for zone count, file count, decoded bytes, voice count, and JSON depth. Reject incomplete bundles; stage import as a temporary directory and rename only after validation. Sample data is not committed to this repo or included in a plugin release.

## Bundle contract, version 1

`instrument.json` records schema version, name, source format/name, importer version, ordered groups, and zones. Each zone has a relative WAV path and SHA-256, key/velocity low-high (inclusive), root note, cents, gain dB, pan, playback start/end in source frames (end exclusive), one-shot, trigger (attack/release), round-robin group/position or random group, exclusive/choke group, and at most one supported loop (start inclusive, end exclusive, mode forward or sustain). Amp envelope has attack/hold/decay/sustain/release; optional filter has cutoff, resonance and key tracking. A separate import report lists every field dropped, approximated, or rejected. Use explicit null/absence for unknown fields, never fabricate a loop or root key silently.

WAV assets are normalized to PCM16 or PCM24, mono or stereo; preserve source sample rate in the WAV header. The engine resamples to 44.1 kHz with a bounded interpolator. A higher-quality resampler is a later optimization after CPU measurements. Keep 24-bit precision in the decode path before final int16 output. The importer must reject unsupported channel layouts and malformed/oversized audio with a clear error.

## Playback semantics

- Note-on filters zones by MIDI key and velocity, then selects one zone per round-robin or random group; independent layers can sound together. Reset a round-robin sequence per instrument instance. Velocity and key crossfades multiply zone gain where present.
- Each voice keeps its own cursor, pitch increment, ADSR, filter, and note/channel identity. Pitch uses sample rate, root note, zone cents, global transpose/tune, MIDI bend and key tracking. Sustain pedal defers note-off. One-shot zones ignore note-off. Release-trigger zones start on note-off. A choke group fades prior voices quickly to avoid clicks.
- Forward and sustain loops are required for v1. Reverse, alternating loops, loop crossfade, scripts and format-specific effects are reported as unsupported until implemented and tested. Stop at the zone playback end; guard zero-length loops and out-of-range metadata.
- Cap polyphony (default 16, choices 8/16/24/32), steal the quietest released voice first, then the oldest active voice with a short ramp. Bound all per-block work; underflow is silence, never a wait. Record max render time and underruns for offline/device diagnostics.
- Each plugin instance owns voices and controls. Shared decoded samples may use immutable reference-counted storage with a bounded cache. Project save stores bank/instrument identity, schema version and overrides, not absolute paths or sample bytes. On missing files, show an error and output silence; never substitute a different numbered patch.

## MPC controls

Parameter order in `params.json` is the stable VST index and must never be reordered after release. The first 16 performance parameters fit one Q-Link page; two further pages cover shaping and playback. All controls are touchable; a readout shows instrument name, loading/error status and zone count. Bank/program selectors read a prebuilt index of bundle IDs, not arbitrary path text. Program changes use a background load. Refresh is a deliberate trigger and never scans in `render()`.

| Page | Q-Links / controls | Engine meaning |
| --- | --- | --- |
| Play | Bank, Program, Level, Pan, Transpose, Fine Tune, Velocity Curve, Polyphony, Pitch Bend, Attack, Release, Filter Cutoff, Resonance, Sustain Pedal, Round Robin Reset, All Notes Off | Frequent performance adjustments; bank/program indices are stable within an indexed library snapshot. |
| Shape | Amp Hold, Decay, Sustain; Filter Enable, Key Track; Velocity to Amp, Velocity to Filter; Start Offset; Loop Enable; Loop Crossfade (future) | Global offsets layered over imported zone settings; a disabled/unsupported feature is shown as unavailable, not silently applied. |
| Zones | Selected Zone, Zone Level, Zone Pan, Zone Tune, Zone Start, Zone End, Zone Loop Start/End, Zone Mute, Zone Solo | Phase 2 editing; initial release is read-only zone inspection to keep chunk state and Q-Link semantics tractable. |

`params.json` below defines the **initial playable controls**. Strings for names/status need a wrapper display-refresh path when an asynchronous load completes (`audioMasterUpdateDisplay` deferred to the audio callback); that wrapper extension is an implementation gate. A skin layout and preview must be built offline before device deployment. The initial preset browser is numeric with a text readout because MPC's native VST2 drop-down is unavailable and the skin cannot accept typed paths.

## Delivery order and gates

1. Define bundle JSON schema and golden import fixtures for SFZ, SF2 and a WAV instrument. Verify zone map, sample checksums, loop semantics, and an import report. Add more formats only with fixtures.
2. Build a host-testable C++ engine from the bundle contract. Test overlapping velocity layers, round robin, one-shot, sustain, choke, pitch, missing/corrupt assets, patch swap, multiple instances and project chunk round-trip under ASan/UBSan. Offline output must be deterministic for a fixed MIDI sequence.
3. Generate the VST2 wrapper, native skin, Q-Link map and offline previews. Verify dynamic readouts and UI updates on the host harness.
4. Cross-compile ARM with `-Wl,--no-undefined`; inspect `VSTPluginMain` export and glibc versions. Benchmark **on device** with realistic voices and storage, including patch switching. Keep real-time CPU comfortably within the 2.9 ms block budget.
5. Only after these gates, stage/install on a device with the user's approval for any MPC restart. Package the plugin separately from user sample bundles and meet catalog metadata/release checks.

Open design decisions: import on the desktop only versus an optional device-side importer; exact memory budget and cache size for the target MPC; whether phase 2 should allow editing zones on the touchscreen. None blocks the initial SFZ/SF2/WAV playable milestone.
