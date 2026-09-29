# Prophet-5 Rev 3.0 — MPC VST2 prototype

This folder contains the first MPC adapter and a provisional control map for VES/MAME's `prophet5r30` driver.
The adapter source, parameter manifest, and VST build configuration are in place. The ARMv7 MAME build is in progress; the plugin is not yet linked, host-tested, or installable.

The VES build helper creates the local Docker image `mpc-vst-arm32v7:12` with the MAME/SDL link dependencies. Use that image for the VST link (`MPC_ARM_IMAGE=mpc-vst-arm32v7:12`) rather than reinstalling the dependencies in the stock compiler image.

## Controls

The 24 Prophet panel pots map to MIDI CC 20–43 in MAME's `pot_0` through `pot_23` order. Parameter values are 0–127; the engine bridge maps them to MAME's 0–240 pot inputs and explicitly refreshes the Prophet ADC mux. The three control pages are grouped as Performance, Oscillator & Mixer, and Envelopes; a separate status readout reports emulator startup.

MIDI note events use VES's virtual Prophet keyboard retrofit. The MPC build disables VES's Lua layout plugin because its desktop panel UI is replaced by the MPC skin. The emulator engine renders on its worker thread; the VST audio callback only sends queued MIDI/CC messages and drains the non-blocking audio ring.

## Private ROMs

Use the private shared MAME ROM folder, configured locally as `MAME_ROMS_DIR`. For a device, copy the legally obtained MAME set separately to `/sdcard/MAME/roms/`; ROMs must never be added to this repository, Docker image, plugin, or release archive. This local folder was empty when the prototype was resumed, so ROM audit/audio boot testing is still blocked.

## Remaining gates

1. Complete a reproducible reduced MAME build for the MPC's 32-bit ARM/glibc target and link audit (`--no-undefined`).
2. Link the VST2 and verify exported ABI, generated skin, and ROM-independent startup/error behavior.
3. With the user's ROM set in the private shared ROM folder, run an audio-paced integration test for startup, parameter-to-CC, note/audio, chunk state, multiple instances, and teardown.
4. Benchmark actual worker-thread CPU on an MPC Live II. Do not trust an unpaced host bench for this wall-clock-paced emulator.
5. Only then request device install/restart approval.
