# QUADWEAVE implementation brief

Target: MPC Live 2, user-reported firmware 3.9.1, ARM32 Linux VST2.
Goal: four independent, host-synced MIDI/.progression players with rhythmic speed ratios, directions, input-note transposition, key/scale mapping and a native MPC browser.

This is a MIDI generator using a custom wrapper, not a block-rendering synth. It reuses the repository ABI and skin/parameter tools. The browser and filesystem parser are separate from the audio callback. No third-party instrument is hosted.

Acceptance: deterministic rhythm and note lifecycle tests; plugin chunk and instance isolation; skin inspection; ARM binary/ABI/dependency check; then four-destination Live 2 routing and timing/CPU measurements. See `../../docs/QUADWEAVE_DESIGN.md` for the wider roadmap and README for the implemented subset.
