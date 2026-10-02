# MPC-MOD — project focus and device diagnostics

Updated: 2026-10-01. Owner: Calvin Highman.
Read alongside AGENTS.md; this brief does not replace the repository's engineering rules.

## Objective and scope

Develop and debug community modifications and native VST2 plugins for the user's Akai MPC Live 2, with reproducible evidence and recoverable changes. This repository is a VST2 porting kit, not a complete firmware source tree. Obtain the exact community-mod repository and revision before proposing firmware patches.

User-reported target: MPC Live 2, firmware 3.9.1; ARM32 builds only. Build workstation: Intel Mac mini, six-core i5, 32 GB RAM, macOS, Docker. Confirm current device and host versions when connecting. Kernel architecture alone does not establish the MPC process ABI.

## Read on every resumed task

1. AGENTS.md and this file.
2. docs/AGENT_WORKFLOW.md and the active port's BRIEF.md / STATUS.md.
3. Relevant docs/NOTES.md sections, plus actual current source/configuration.
4. docs/PORTING.md, docs/SKIN_STUDIO.md, docs/BENCH.md and docs/RELEASING.md as applicable.
5. Check the current git revision and working changes; resume the next incomplete gate.

Guidance reviewed at commit c5a3ec803c554ca12da9a89065602e81b4bea697. Force findings in NOTES are evidence about that tested Force setup, not proof of Live 2 compatibility.

## Keep the work focused

- Choose one reproducible bug or acceptance criterion per change.
- Record observed facts separately from hypotheses and planned functionality.
- Reuse the wrapper, build tools, device probe and verification workflow.
- Target ARM32 / VST2; inspect ELF class, ABI, exported VSTPluginMain and dependencies.
- Preserve released IDs and parameter indices. Keep file/network work off audio callbacks.
- Do not assume native Akai instruments/effects expose a reusable hosting API.
- No firmware, ROMs, private samples, proprietary assets, credentials or device addresses in git.
- Mark unavailable tests as NOT RUN; host success is not hardware validation.

## Access and tools

- GitHub: source, issues, reviewable changes, CI results and durable checkpoints.
- Remote Desktop Commander: candidate bridge from this chat to the user's Mac terminal; discovered but not connected on 2026-10-01. Verify host compatibility and permissions during setup.
- Local Codex CLI on the Mac: alternative execution environment with local terminal access.
- SSH from the Mac to the MPC: requires an already enabled SSH service and authorized credentials. A plugin does not root the MPC or grant device access by itself.
- Tailscale: optional private networking across locations. Use a supported gateway/subnet router if the MPC cannot run a client. It does not grant SSH credentials or root.
- Codex Security: optional source security analysis; not a substitute for audio debugging.

Keep SSH private (LAN or private network), verify the host key and keep private keys on the Mac. Use the least privilege that can read the diagnostic data. If the existing mod exposes only root, keep initial operations observational and log commands locally.

## First connection gate

Confirm exact community firmware/mod name, source URL, revision, installation method, current OS version, SSH availability and recovery method. Do not infer these from the VST repository.

Initial discovery, once SSH works:
- Identity and system: id, uname, OS release.
- MPC process/binary ABI, libc, available utilities and actual settings paths.
- Existing service manager and log sources; do not assume systemd or a journal.
- Available storage, current mounts and a baseline of selected plugin/configuration files.

Review tools/probe_device.sh before running it. Although described as read-only, it creates and removes a temporary strings file in /tmp. Its architecture output requires manual validation if the MPC binary is absent or unreadable. Do not treat a probe's lack of matches as proof of unsupported functionality.

## Live monitoring design — not implemented yet

Run a bounded collector from the Mac, outside MPC's audio process. Continuous collection is performed by that local process, not by an idle chat.

1. Timestamp a baseline: firmware/mod/build revisions, selected file hashes, process state and available logs.
2. Follow actual application/system logs; select journalctl, logread or tail only after discovering what exists.
3. Watch narrow plugin/skin/settings directories using inotify if available. Handle rotation, directory replacement, overflow and reconnection; rescan after gaps.
4. Fall back to modest-interval metadata/hash snapshots of small selected files if no watcher exists. Avoid scanning sample libraries or the entire filesystem.
5. Sample CPU, resident memory, thread state and free storage at a measured low rate. Compare audio behavior with the collector enabled and disabled.
6. Record the user's reproduction action and timestamp. Correlate file events with crashes, underruns and state changes; a file change alone is not a bug diagnosis.
7. Rotate and cap logs on the Mac; preserve disconnect/gap markers. Redact sensitive data before sharing or committing evidence.

Defer strace, debugger attachment and core-dump configuration until a specific failure warrants their overhead and storage impact.

## Change and validation gates

- Back up affected settings/files and record checksums before changing them.
- Follow AGENTS.md: obtain authorization before stopping/restarting MPC or running an installer that restarts it.
- Edit registration settings only with MPC stopped; validate XML and preserve unrelated entries.
- Stage binaries, verify checksums and keep a known-good rollback copy.
- Host tests and binary/skin inspection precede device deployment.
- Device acceptance: insert, audible playback, note-off, controls/Q-Links, automation, save/reload and multiple instances, followed by CPU benchmark.
- Firmware flashing, boot changes and system-library replacement require a separate explicit scope and recovery plan.
- Record model, firmware, date, commands and outcomes. Publish hardware claims only after actual verification.

## Active plugin design

[QUADWEAVE design template](docs/QUADWEAVE_DESIGN.md), added 2026-10-01: a four-track rhythm/arp/melody/chord MIDI generator inspired by Plinky 12 Toadstep and Xfer Cthulhu. Includes explicit polyrhythm/polymeter clocks, harmonic conductor, six-page MPC UI, four-knob mappings and staged acceptance gates. Working name; see the playback implementation checkpoint below for current code and verification status.

Design v0.2 adds core MIDI File mode: browser/audition, polyphonic SMF playback, live input-note transposition, source/target key and scale mapping, per-lane output channel remapping, and embedded clip persistence. See section 14 of QUADWEAVE_DESIGN.md. The implemented playback/browser subset is recorded in the checkpoint below.

Next plugin gate: test one ALSA port with four output channels and simultaneous destination playback/recording on the Live 2; test separate ports if needed. Access discovery below remains a prerequisite for on-device work.

## Playback implementation checkpoint — 2026-10-02

The `ports/quadweave/` implementation now contains four independent PPQ-synced players, straight/ratio speeds, Forward/Reverse/Ping-pong, MIDI and MPC `.progression` loaders, key/scale transforms, live input transpose, per-track output channels, native browser pages and embedded clip state. Read its README and STATUS for the implemented subset and test evidence. Host tests and six-page previews pass; ARM compilation and Live 2 hardware tests remain NOT RUN. The wider design remains a roadmap, not a claim that every feature exists.

Next build command on the Mac: `ports/quadweave/build.sh`. Do not install an x86 host build on MPC.

## Current checkpoint

Completed:
- Reviewed root agent rules, agent workflow, README, relevant hardware notes and device probe.
- Established ARM32 Live 2 focus and the diagnostic workflow.
- Discovered a possible Mac terminal bridge.

Not done:
- No Mac/MPC connection, root session, firmware change, monitoring process or device test.
- Community-mod identity/source and current access/recovery state remain unknown.

Next action:
Confirm community-mod identity and existing SSH access, then connect an authorized Mac execution environment and collect the baseline.

## Session handoff template

- Date / goal:
- Repository / branch / exact commit:
- Device model / OS / community mod revision:
- Reproduction steps / expected / observed:
- Evidence and local log location (no private addresses or secrets):
- Changes and backup / rollback:
- Tests: PASS / FAIL / NOT RUN, with commands:
- Remaining uncertainty:
- Next concrete action:
