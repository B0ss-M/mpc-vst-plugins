# Fast workflow for a new MPC plugin

## 1. Define the task

Copy `templates/plugin/BRIEF.md` and `STATUS.md` into the port. Record the plugin kind, engine source and revision, device/firmware target, data requirements, controls and acceptance criteria. Use `src/VENDORED.md` for source provenance. Classify the engine with PORTING before writing an adapter. The Prophet-5 folder is an unfinished external MAME integration, not a generic starter.

## 2. Scaffold a block-rendering instrument

From this repository root:

```sh
python3 tools/new_port.py ports/my-synth --name "My Synth" --vendor "My Vendor" --uid MyS1
./tools/test_port.sh ports/my-synth/vst.json
./tools/build_port.sh ports/my-synth/vst.json
```

Requires Python 3, gcc/g++ (or Docker for the host test), and Docker with ARMv7 emulation for the ARM build. The script refuses an existing destination. Use a unique four-character ASCII UID and keep it fixed after shipping. It checks local manifests for UID/library collisions; check external ports separately. Paths and source lists are generated relative to the port, so a sibling port repository works too.

The starter provides an audible monophonic sine engine with velocity, note-off, CC120/123, gain and chunk state. Replace its DSP with the real engine. It creates `vst.json`, `params.json`, `src/engine.c`, README, brief, status and provenance templates. No license is selected automatically: resolve licensing before distribution.

Effects use `effect: true` and the ABI's `process` callback; MIDI generators need ALSA routing and often a custom wrapper. Samplers need explicit format coverage, key/velocity zones, root pitch, loops, tuning, missing-file behavior and licensed fixtures. These are design tasks, not capabilities supplied by this instrument starter.

## 3. Implement and map

Implement the ABI and verify each parameter against the engine. Preserve parameter order, including hidden popup controls in shipped layouts. Keep heavy loading off audio threads. Save all musical state with versioned validation; do not save transient notes or popup flags. Add engine-specific tests for audible output, boundary controls, persistence and any worker queues. `host_test.c` reports silent audio and absent chunks as warnings, so PASSED alone is insufficient for a playable instrument.

## 4. Validate the page and binary

Build the ARM library using the existing build tool. Inspect its architecture, exported `VSTPluginMain`, dependencies and unresolved symbols. Make the skin deliberate with `tools/studio.py` and follow SKIN_STUDIO for preview syntax; inspect every page, text, mode and popup state. Record the exact commands and artifacts in STATUS.

## 5. Verify hardware and package

Run BENCH on the target hardware; wall-clock worker engines also need paced CPU measurements. Complete insert/play/Q-Link/automation/save-reload/multiple-instance testing. Record actual model, firmware and date, then package using RELEASING and validate the archive with `catalog_check.py --catalog`. Publish only after the relevant gates pass and required device actions are authorized.

## Resume a task

Read AGENTS, the port's BRIEF/STATUS and relevant NOTES sections. Check current source/config against the recorded checkpoint. Start with the next uncompleted gate; do not repeat expensive emulator builds unless inputs changed or evidence is missing. Record the exact dependency checkout/revision and image used for reproducibility.
