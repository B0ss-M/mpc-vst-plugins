# QUADWEAVE ARM32 — first hardware test

This is a compiled test candidate for a community-modded MPC Live 2, not a firmware update. It produces MIDI, not sound. The ARM build and host tests are verified separately; Live 2 insertion, routing, timing and project recall need your feedback.

## Install

1. Save and close your current project. Start testing in an empty project.
2. Extract the ZIP and copy the QUADWEAVE folder to the MPC's storage.
3. Read INSTALL.md inside that folder. From an existing root terminal on the MPC, change to the extracted folder and run `sh install.sh`.
4. The installer asks before stopping/restarting the MPC application and backs up MPC.settings. It installs `/sdcard/vst/quadweave.so` and its skin. Run this on the MPC, not macOS. This package does not root the device.
5. Create `/sdcard/vst/quadweave-midi` if absent. Copy the contents of Sample-Files there. You may add your own `.mid`, `.midi`, `.progression` files and subfolders. The installer does not replace this collection.

## First playback

1. Insert QUADWEAVE on a plugin track. Open Status: record the displayed ALSA output ID or error.
2. Add a separate instrument destination. Look for QUADWEAVE / MIDI Out as its MIDI input, channel 1; enable the MPC's required monitoring. This routing is the main Live 2 compatibility test.
3. In QUADWEAVE Browser, select `First-Test.progression`, Load Track A, File Track 0, File Ch 0, then Load. Track A: enabled, speed 1x, Forward, output channel 1. Start MPC transport.
4. Repeat with `One-Beat.mid` on A and B; use separate destinations/channels 1 and 2. A speed 5:4, B speed 7:4 gives five and seven attacks over four host quarter-note beats.
5. Use `Four-Notes.mid` to compare Forward, Reverse and Ping-pong; the latter alternates complete forward and reverse cycles. Try Fit Scale with a new target key/scale. Enable Input Root to test keyboard/pad transposition around anchor note 60.
6. Stop/start, mute, change channel and remove the plugin; listen for stuck notes. Save/reopen the empty test project to check clip recall.

## Send feedback

- MPC model, firmware and community-mod version; ZIP/build name.
- Does QUADWEAVE appear and insert? Do all six pages fit the screen?
- Status page output text and whether the MIDI port appears on destination tracks.
- Which file, track, speed, direction and key/scale reproduce a problem?
- What you expected versus what happened; a short recording or screen photo helps.
- Does stopping release every note? Does saving/reopening preserve the clips?

This first build implements file/progression playback and transforms. Generated arp/melody editing, Euclidean patterns, scenes, favourites and export remain planned. MIDI output is block-timed, with additional worker/routing jitter still to measure.

Use `sh uninstall.sh` from the installation folder to remove registration and the plugin/skin; it also asks before restarting MPC. Your MIDI collection is separate.
