# Agent instructions for MPC plugin development

This repository is the shared MPC OS VST2 porting kit. Read this file before editing.

For MPC-MOD community modification and Live 2 diagnostics tasks, also read [MPC_MOD_PROJECT.md](MPC_MOD_PROJECT.md) and update its checkpoint when work progresses.

## Start with the relevant sources

- `docs/AGENT_WORKFLOW.md`: short task workflow and verification gates.
- `wrapper/engine.h`: actual engine ABI; `tools/params.py` and `tools/gen_vst.py`: manifest formats.
- `docs/PORTING.md`: engine, parameter, skin and distribution checklist.
- `docs/NOTES.md`: dated hardware evidence and unresolved limitations. Read the sections relevant to your task; do not promote a hypothesis into a verified fact.
- `docs/SKIN_STUDIO.md`, `docs/BENCH.md`, `docs/RELEASING.md`: skin, device cost and packaging.

## Implementation rules

Reuse the wrapper and tools rather than copying them into each port. Start a block-rendering instrument with `python3 tools/new_port.py ports/my-synth --name "My Synth" --vendor "My Vendor" --uid MyS1`. The starter is a monophonic sine test instrument, not a completed synth or an emulator.

Use per-instance engine state. Render interleaved int16 stereo at 44100 Hz in 128-frame blocks. No disk/network I/O, waits, unbounded allocation or blocking locks in the audio callback. Worker engines need bounded queues, clear ownership and deterministic shutdown.

Keep released UID, library name, parameter keys and indices stable. Append parameters; inspect generated hidden popup indices too before changing layouts. Implement all exposed controls and persistent state; distinguish readouts and triggers from saved musical settings. Integer controls use `display: int`; text uses `display: string`. A successful generic host test does not prove audible output: its audio check can warn rather than fail.

Vendor upstream engine source at an exact commit with its license and `src/VENDORED.md`. Never commit ROMs, firmware, private samples, Akai skin assets, secrets, device addresses or the Steinberg SDK. Do not choose a license for existing third-party code without checking compatibility.

Use the proven skin controls. Native VST2 combo boxes are empty; use segments or drawn popups. Do not assume graph/XY/native meter widgets work. Preview all pages and popup states, and compare an existing app's theme against its reference artwork. Treat dated NOTES entries in the context of current wrapper code.

## Verification and delivery

Run the offline host test and inspect the skin before device deployment. Record commands, outcomes and remaining gates in a port's `STATUS.md` using `templates/plugin/STATUS.md`. Add hardware findings to NOTES with model, firmware and date only after actual verification. Never claim ARM, device or ROM tests passed when they were unavailable.

Ask before restarting MPC or installing an installer that restarts it. Back up settings before registration with MPC stopped; stage library replacements and verify checksums. The device is the user's live setup.

Bench on the target device before release; paced worker engines need real thread CPU measurements. Follow catalog metadata and distribution rules in PORTING/RELEASING. Firmware-derived binaries stay local. Keep documentation and task status aligned with the final behavior.
