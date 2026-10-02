# QUADWEAVE — four-track rhythm, arp, melody and chord generator

Design template v0.2 • 2026-10-01 • MPC-MOD
Working name. Implementation checkpoint (2026-10-02): `../ports/quadweave/` implements the MIDI/progression playback and browser subset. See its README/STATUS; the broader specification below is not all implemented. No hardware verification yet.

## 1. Product brief

A four-track MIDI composition and performance plugin for MPC Live 2, targeting firmware 3.9.1 and ARM32 Linux VST2. One instance generates four coordinated musical parts, each routed to its own MPC instrument or MIDI destination. Every track can be Rhythm, Arp, Melody, Chord or MIDI File; the track names below are starting roles, not restrictions.

The musical objective: create a coherent four-part phrase quickly, then vary its rhythm, register and articulation without losing its harmonic identity.

| Track | Default role | Default mode | Initial output | Colour |
|---|---|---|---|---|
| A | Foundation / bass | Melody | Channel 1 | Amber |
| B | Movement / arp | Arp | Channel 2 | Cyan |
| C | Motif / counterline | Melody | Channel 3 | Violet |
| D | Harmony / chord stabs | Chord | Channel 4 | Green |

This is a MIDI generator. Sounds, synthesis, ADSR and audio effects belong to destination instruments. Do not include a sampler or internal instrument host in v1.

## 2. Design references and original contribution

- Plinky 12 Toadstep, designed with Toadstool Tech: inspiration for direct stage editing, stage repeats, ratchets, probability and performance-oriented pattern changes.
- Xfer Cthulhu: inspiration for chord recall from a trigger note, chord learning, note-index arpeggiation and independently looping modifier lanes.
- Our design adds an MPC-oriented four-part harmonic conductor, explicit polyrhythm versus polymeter controls, constrained melody variation and a four-knob workflow.

Use original code, presets and artwork. These are interaction references, not source dependencies or promises of preset compatibility. The current Toadstep page describes eight tracks; our specification deliberately remains four.

Sources reviewed 2026-10-01:
- https://plinky12.com/toadstep.html
- https://xferrecords.com/products/cthulhu
- Repository: AGENTS.md, MPC_MOD_PROJECT.md, docs/AGENT_WORKFLOW.md, docs/NOTES.md, docs/SKIN_STUDIO.md, poc/midiport.c.

## 3. First technical gate: MIDI routing

Repository evidence says standard VST2 MIDI output is ignored by the MPC host on the tested Force. An ALSA sequencer output port was detected and routed successfully on that Force. This is not yet verified on this Live 2 / firmware / community-mod combination.

Prototype one ALSA port with four distinct channels first. Test simultaneous destination monitoring and recording on four MPC tracks, including while the generator track is not selected. If channel filtering is insufficient, test four named output ports. Do not advertise four-part operation until this passes.

Proposed routing:
- MPC control track hosts QUADWEAVE and receives pads, keyboard or a trigger clip.
- QUADWEAVE outputs A/B/C/D on distinct channels, or distinct ports if required.
- Four destination tracks select the matching source/channel and play MPC instruments, keygroups or external MIDI equipment.
- Disable input/thru paths that feed generated notes back into the generator.
- No internal hosting of Akai plugins is assumed.

Host transport supplies musical position and tempo. The generator's Run control arms generation; it does not claim to start or stop the MPC transport. Confirm the host continues calling a silent generator. Optional development blips are diagnostic only.

The existing poc/midiport.c is a routing reference, not production code: it has shared global state and performs logging/output in the audio callback. Production needs per-instance state, a bounded event queue and an output worker.

## 4. Common track engine

Each track contains:
- Mode: Rhythm / Arp / Melody / Chord / MIDI File.
- Source: live held notes / latched notes / chord slot / shared progression / local progression.
- Pattern A–H; 1–16 stages in v1, with data format room for later extension.
- Independent clock, pattern length, direction, reset policy and output assignment.
- Modifier lanes: velocity, gate, octave, probability and ratchet; each 1–16 entries, independently loopable or linked to stages.
- Register limits, transpose, mute, density and variation lock.
- Up to eight simultaneous generated notes per track, with explicit event limits.
- A saved deterministic random seed.

Generation order:
1. Resolve current chord/source at the event's musical time.
2. Advance the rhythm/stage and read modifier lanes.
3. Evaluate gate, condition and probability.
4. Produce a fixed note, selected chord tones, melody degree or voiced chord.
5. Apply pitch transformations and the selected harmonic constraint.
6. Apply register limits and deduplicate notes.
7. Schedule articulation, ratchets and matching note-offs.
8. Deliver bounded MIDI events to the output worker.

### Track modes

| Mode | Behaviour | Principal controls |
|---|---|---|
| Rhythm | Retrigger a chosen note or captured note set; useful for percussion and chord stabs | Hits, rotation, gate, accent, probability |
| Arp | Select notes by position from the current ordered chord pool | Order, index mask, octave range, traversal, retrigger |
| Melody | Play editable scale-degree steps or generate a bounded motif | Degree, contour, range, chord-tone bias, maximum leap |
| Chord | Play a chord from a slot or progression, with controlled voicing | Degree/slot, quality, inversion, spread, strum |
| MIDI File | Play a polyphonic MIDI clip with input-controlled transposition and key/scale mapping | File, source part, source/target key, transform, trigger, output channel |

Arp orders: Up, Down, Up/Down, Played, Random and Custom. Custom stages can select one or multiple of eight note indices. Missing indices wrap modulo the current note count; an empty note pool is silent. Tied/repeated notes have explicit retrigger rules.

MIDI slide is not a universal synth feature. v1 offers overlapping legato notes only; actual glide depends on the destination instrument. Per-note pitch-bend/MPE is deferred.

## 5. Rhythm model: repeats, ratchets and two clock modes

A stage is a stored musical instruction. A tick is one clock interval. A repeat extends a stage across multiple ticks; a ratchet divides a tick into multiple attacks.

| Parameter | v1 design range | Meaning |
|---|---|---|
| Stage count | 1–16 | Number of active stored stages |
| Rate | 1/1 to 1/32, plus selected dotted/triplet values | Tick length in Step mode |
| Cycle length | 1–16 quarter-note beats | Total loop duration in Cycle mode |
| Repeats | 1–8 per stage | Ticks spent on the stage |
| Repeat gate | First / Every / Hold | Attack behaviour during repeats |
| Ratchet | 1–8 per tick | Attacks subdividing an enabled tick |
| Gate | 5–95% | Note duration within a ratchet interval; Tie is separate |
| Probability | 0–100% | Chance of firing an enabled tick |
| Rotation | 0 to stage count minus 1 | Rotate the stage pattern |
| Swing | 0–60% of one tick's duration | Delay odd ticks; preserve pair length |
| Direction | Forward / Reverse / Ping-pong / Random | Stage traversal |
| Condition | Always / odd loop / even loop / every N / Fill | Deterministic gate condition |

**Step mode:** ticks have a fixed musical duration. Differing stage counts/repeat totals yield different loop lengths. For example, 5 ticks against 7 ticks at the same 1/16 rate is polymeter; both restart together after 35 ticks.

**Cycle mode:** all expanded ticks fit inside the selected cycle. If W is the sum of stage repeats and B is the cycle length in quarter-note beats, tick duration is B/W. Five stages and seven stages, each with repeat=1 and B=4, create a true 5:7 polyrhythm over one 4/4 bar.

For an exact N:M demonstration, use all gates on, probability=100%, swing=0 and ratchet=1. Euclidean rhythm is a separate gate-mask generator (K hits across N positions), not a synonym for polyrhythm.

Cycle mode v1 uses forward traversal. Other directions are Step-mode features until their cycle timing is specified and tested. Modifier lanes advance per expanded tick, including silent ticks; ratchets reuse that tick's modifiers. A reset resets their cursors too. Swing requires an even expanded-tick count in v1; disable it with a clear readout for odd counts.

Euclidean masks replace the rhythm gate mask on request. Manual edits remain until Generate is pressed again. Density scales effective firing probability rather than rewriting saved steps. Show effective probability.

## 6. Harmonic conductor and chord memory

Global controls: root, scale, chord source, progression position, change quantisation and reset policy. Each track can follow shared harmony or explicitly use its own source.

Chord memory:
- 16 labelled slots, each holding up to eight explicit MIDI notes.
- Learn a chord from incoming notes; collect the union of notes until all keys are released or Learn is confirmed. Commit the complete set atomically.
- Deduplicate pitches; preserve stored voicing and show actual notes.
- One input note can recall one slot. The configurable trigger range is consumed by the control layer, not echoed to destinations.
- In v1, learning stores notes without promising automatic chord-name recognition. Labels may be generic Slot 01 etc.
- Preset names use fixed labels/numbered slots on MPC; arbitrary text entry is not assumed.

Progression:
- Up to 16 events, each with degree or explicit chord slot, chord quality, inversion and duration in beats.
- Separate duration from each output track's rhythm: changing a rhythm does not unexpectedly advance harmony.
- One harmonic state is resolved for all following tracks at each timestamp.
- Diatonic mode builds qualities from the selected scale. Chromatic/custom chords are explicit choices, never silently corrected.
- Suggested progression templates are curated musical options, not a claim of objective harmonic correctness.

Constraint modes:
- Chord Lock: every generated pitch belongs to the current chord.
- Scale Lock: melody can use non-chord scale tones; optional strong-beat chord-tone bias.
- Free: retain explicit pitches, including chromatic notes.

On chord change, default to releasing prior generated notes and using the new chord at the boundary. An optional Common-Tone Hold retains exact shared pitches without retriggering. Intentional overlap must be a visible option, not a stale-note bug. Notes due before a change keep the old chord; notes due at or after it use the new chord.

Voice leading:
- Build a bounded set of candidate inversions/octave placements inside the track range.
- Score total voice movement, large leaps and voice crossing; optionally fix bass to root.
- Preserve chord quality and pitch classes. If the requested full voicing cannot fit, show an explicit range warning and remain silent until resolved rather than silently dropping defining tones.
- State whether a melody is a chord tone, scale tone or deliberately chromatic; never label all scale tones as chord tones.

## 7. Melody and controlled variation

Provide manual degree entry first, then a deterministic Generate action.

Generation inputs: seed, scale/chord source, lowest/highest note, motif length, contour (Rise / Fall / Arch / Flat / Random), maximum leap and chord-tone bias.

Performance tools:
- Mutate Rhythm, Mutate Pitch and Mutate Both.
- Amount affects only unlocked stages and eligible values.
- Lock pitch, rhythm, velocity or the entire stage.
- Keep/Undo offers a one-level musical-pattern snapshot, not transport rollback.
- Freeze stores the current generated result as editable pattern data.
- Randomness must reproduce on replay from the same seed and musical position; seeking does not depend on wall-clock randomness.

A later phrase-generation stage may add call-and-response and cadence templates. v1 does not claim AI composition or audio analysis.

## 8. MPC screen template

Visual direction: dark charcoal background, pale readable labels, one accent colour per track and consistent 4/4 grouping. Track letter, mode and mute state remain visible so colour is not the only cue.

Use repository-supported buttons, option segments, faders/knobs, text and static artwork. A 16-step editor is made from ordinary selectable controls; do not depend on a custom piano roll, draggable note grid, native combo box or animated waveform.

| Page | Top area | Main area | Bottom controls |
|---|---|---|---|
| Play | Preset, key/scale, Run, transport readout | Four track strips: mode, pattern, mute, step readout | Density, variation, swing, register |
| Rhythm | Track and pattern selector | 16 stage selectors; selected-stage repeat/gate/probability/ratchet | Rate/cycle, length, rotation, Euclidean |
| Notes | Track, source, mode | Note-index/degree editor and velocity/gate/octave lane selectors | Order, register, contour, locks |
| Chords | Global/local source and current slot | 16 slot selectors, note-value readouts, progression event editor | Learn, quality, inversion, spread |
| Scenes | Scene A–H and pending launch | Four pattern assignments plus global progression assignment | Launch quantisation, chain, undo |
| Setup | Output status and instance name | Channels/ports, input filter, reset/latch settings | Panic, save slot, later MIDI export |

Add Browser and Clip as nested pages under Notes; keep six top-level tabs.

Six short tab names: Play, Rhythm, Notes, Chords, Scenes, Setup. Use nested pages for depth and keep subpage labels short. The reference kit's Force canvas must not be assumed to match Live 2 geometry: capture the actual plugin viewport, preview every state and validate touch targets on device before freezing pixel dimensions.

Main interaction: select a track, select a stage, then edit four prominent controls. Physical pads remain note/slot triggers by default. Optional Pad Edit mode can select stages only after received note mappings are verified; do not assume direct pad LED or dedicated-button control.

### Four physical Q-Link controls

These are proposed physical knob roles. Translate them through the repository's logical Q-Link map and verify Live 2 bank order.

| Bank on Rhythm | Knob 1 | Knob 2 | Knob 3 | Knob 4 |
|---|---|---|---|---|
| Timing | Rate / Cycle | Stage count | Rotation | Swing |
| Stage | Repeats | Ratchet | Gate | Probability |
| Expression | Velocity | Accent | Octave | Density |
| Variation | Seed | Mutation amount | Condition | Euclidean hits |

On Play, use four directly bound controls for A/B/C/D density, then a bank for A/B/C/D mute, then A/B/C/D octave. Avoid making host automation depend on whichever track happens to be selected.

Per-step editor fields are not host-automatable in v1; they edit chunk state. Expose stable per-track performance parameters for automation. No reused parameter index may silently target another track/stage after UI navigation.

## 9. Patterns, scenes and persistence

Each track has eight stored patterns. Eight scenes reference one pattern per track plus a global progression and performance settings.

Quantised launch choices: next beat / next host bar / end of master cycle. A scene switches all four track references at the same timestamp. Per-track phase policy is Reset or Continue; default Reset. Flush or retain notes according to the explicit chord/tie policy. Respect host time signature when calculating a bar; do not assume four beats outside the examples.

Save a versioned VST chunk containing all pattern banks, chord slots, progression events, scenes, seeds, mappings and settings. Never save active notes, pending note-offs, raw ALSA port IDs, worker handles or transient UI popup state. Resolve external resources on load and remain safely stopped if routing fails.

Later MIDI export: Standard MIDI File type 1 with four musical tracks plus a conductor/meta track, explicit render length, tempo/signature events and finite note-offs. Snapshot musical state and write off the audio thread. This is file export, not a promise of native drag-and-drop into MPC.

## 10. Engine architecture and reliability

Proposed modules: transport, harmony, rhythm, arp, melody, voicing, event scheduler, MIDI output, state, UI parameter adapter. Pure musical logic must run without ALSA for deterministic offline testing.

Use host PPQ/tempo and validated time flags. Compute phase from musical position with integer/rational counters where practical; avoid accumulated floating-point drift. Handle stop/start, loop wrap, seek, tempo changes and invalid host time explicitly. Missing valid transport means flush and suspend, not run at an invented tempo.

An audio callback builds bounded events without allocation, logging or blocking I/O. A per-instance worker owns ALSA output. Stop/unload/routing changes cancel queued future events and flush tracked notes before port teardown. Maintain note ownership per port/channel/pitch, including retriggers and overlapping notes.

Reserve capacity for note-offs. On overload, reject excess new attacks, report saturation and recover without hanging notes; never silently lose note-offs. Bound per-stage notes, ratchets and generated-event density. Rate-limit expensive UI readouts.

The demonstrated routing sends at block granularity: 128 samples at 44.1 kHz is about 2.90 ms before additional scheduling/routing effects. Internal timestamps and an output worker do not prove sample-accurate delivery. Measure actual MIDI jitter and ratchet spacing on Live 2. Suppress unsupported microtiming settings or state their effective resolution.

Panic sends targeted note-offs and sustain-off on owned destinations; avoid broad all-channel resets affecting unrelated tracks. No background MIDI clock output unless explicitly implemented and enabled; MPC host transport remains the master.

## 11. Example starting patch: Minor Orbit

Key: C natural minor. Tempo: 120 BPM. Shared progression, one 4/4 bar each:
Cm7 [C Eb G Bb] -> Abmaj7 [Ab C Eb G] -> Ebmaj7 [Eb G Bb D] -> Bb7 [Bb D F Ab].

| Track | Mode | Clock | Material |
|---|---|---|---|
| A | Melody | Step, 8 ticks at 1/8 | Root/fifth bass motif, low register |
| B | Arp | Cycle, 5 ticks in 4 beats | Ascending chord indices, middle register |
| C | Melody | Cycle, 7 ticks in 4 beats | Editable chord-tone motif, upper register |
| D | Chord | Step, 4 ticks at 1/4 | Four-note voiced stabs |

B and C form a 5:7 pulse relationship. Start with all gates enabled and no swing/ratchets; add probability afterwards. D and following tracks read the same chord boundary. Choose octave ranges that preserve the full voicings.

Alternate patch: use B with 5 stages and C with 7 stages, both at fixed 1/16 Step rate, to demonstrate polymeter separately.

## 12. Build sequence and acceptance criteria

| Gate | Deliverable | Must demonstrate |
|---|---|---|
| 0 | Live 2 routing prototype | Four destinations, independent channels/ports, host callback continuity, no feedback |
| 1 | Transport and note lifecycle | Repeatable timing, stop/seek/unload cleanup, multi-instance isolation |
| 2 | Four-track rhythm engine | Step/Cycle clocks, exact internal 5:7 positions, repeats/ratchets, reproducible probability |
| 3 | Harmony and pitch modes | Chord recall, arp/melody/chord modes, shared boundaries, correct note sets |
| 4 | Skin and persistence | All pages usable, Q-Link mapping, automation identity, save/reload |
| 5 | Performance features | Scenes, Euclidean masks, constrained mutation, locks/undo |
| 6 | Release gate | ARM binary checks, device CPU/jitter measurements, routing/play/record stress test |

MVP includes gates 0–4 with basic patterns, manual harmony and the MIDI File browser/playback requirements in section 14. Add a file-player gate after transport/note lifecycle and before final skin validation. Advanced generation, chaining, MIDI export, CC lanes and MPE remain deferred; MIDI import/playback is now core scope.

Required tests:
- 5:7 over one bar produces the correct event counts and rational timestamps; no drift across many cycles.
- 5/7 Step-mode patterns align after 35 ticks when all repeats are one.
- Chord Lock never emits foreign pitch classes; custom chords remain exact.
- Simultaneous chord changes do not produce an accidental union of old/new voicings.
- Empty input, out-of-range voicing and malformed state fail predictably.
- Every attack receives a release across mute, stop, panic, seek, scene changes, output loss and unload.
- Event saturation preserves release capacity.
- Two instances have independent ports, queues, seeds and state.
- A silent MIDI generator is judged by recorded MIDI and destination playback; a generic audio warning is expected, not proof of failure or success.
- Save/reload preserves patterns and automation bindings without restoring held notes.

## 13. Current status and next action

DESIGN ONLY. No source implementation, compiled skin, ARM build or device test is delivered by this document.

Next implementation action: create the minimum one-port/four-channel routing experiment following the repository rules, then verify it on the user's Live 2 before implementing the full interface. Record exact community-mod revision and destination monitoring/recording behaviour.

Before release, reserve a unique four-character VST UID after checking existing ports; do not treat the working name as a final product identity.


## 14. Smart MIDI browser and file-player mode

Added 2026-10-01 at the user's request. This section extends the core design; it does not indicate an implemented browser.

### User workflow

Browse -> audition -> assign to A/B/C/D -> choose source key/scale -> set target key/scale -> choose input behaviour -> choose output channel -> play/record.

Each track can hold a different MIDI clip. A multitrack file can instead assign up to four selected source parts to A/B/C/D. Preview is MIDI sent to a selected destination instrument, so the browser itself makes no sound.

### Browser template

- Scan configured user folders for .mid and .midi files in a background worker.
- Paginated file/folder list, folder Up, Previous/Next, Refresh, Favourite, Preview, Load and Stop Preview.
- Show filename, source track/channel list, note count, duration in beats/bars, time signature, pitch range, polyphony, tempo metadata and key metadata.
- Bar count requires a known meter; show beats if meter is missing or ambiguous.
- Filter by folder, favourites, length, estimated role (bass/chord/melody/drums) and source key. Estimated roles and keys carry confidence labels.
- Filename tags and declared key-signature events are clues, not proof. Analyse pitch-class distribution and duration as an optional suggestion; show alternative keys or Unknown for ambiguous material. Never silently commit an uncertain source key.
- No native file-dialog API or touchscreen text entry is assumed. Use fixed result slots with live text and buttons; filename text search may be added in a companion editor later.
- Preview uses a dedicated destination or temporarily suspends only the selected generator track. Never accidentally layer preview over its old clip. Stop Preview releases its notes and restores the prior track state.

### Loading contract

Initial scope: Standard MIDI File format 0 and format 1 using PPQ timing, including polyphonic notes, note-on velocity zero as note-off, tempo/time-signature/key metadata and sustain CC64. Parse valid running status and track boundaries. Format 2 and SMPTE timing produce a clear Unsupported message in v1 rather than incorrect playback.

Source selection distinguishes SMF track from MIDI channel: one file track can contain several channels. Show both. Let the user select one part or intentionally merge selected parts. Do not silently collapse a full arrangement onto one output channel. Assigning more than four parts requires an explicit selection.

Default event policy: notes and sustain; program/bank changes, SysEx, pitch bend and other controllers are filtered and reported. This prevents an audition from changing the destination preset. Bend-dependent performances may sound different; show a warning when filtered events exist. Broader controller support is a later explicit option.

Preserve original tick positions, durations, velocities and polyphonic simultaneity in an immutable source clip. Do not reduce MIDI File playback to the 16-stage editor or apply generator ratchets by default. Transform non-destructively; Reset restores the source.

Keep parsing, indexing and allocation outside the audio callback. Initial resource caps: 8 MiB/file, 100,000 parsed events/file, 64 file tracks and 32 simultaneous notes per loaded lane, with a bounded total of 128 active notes across four file lanes. These are design limits to benchmark, not measured capacity. Reject over-limit clips with a message rather than truncate them silently. The eight-note generated-chord limit elsewhere does not cap MIDI File clips.

Validate chunk sizes, variable-length values, cumulative tick overflow, event lengths and missing releases. Report any offered repair; do not silently invent a different performance. A failed load leaves the existing clip intact.

### Three distinct pitch transforms

| Transform | Behaviour | Use |
|---|---|---|
| Transpose | Shift every note by one signed semitone offset | Preserve all intervals and chord qualities |
| Fit Scale | Apply root shift, then snap out-of-scale notes to the nearest allowed target pitch | Fast adaptation, potentially changes intervals/voicings |
| Degree Map | Map source scale degree and octave to the corresponding target scale degree | Convert major/minor/modal phrases predictably |

Controls: Source Root, Source Scale, Target Root, Target Scale, Transform, Semitone Offset, Octave, Range and Chromatic Policy. Each lane can Follow Global Key or use a local target.

Degree Map v1 requires equal-sized source and target scales. Support major, natural/harmonic/melodic minor (ascending collection), seven church modes, major/minor pentatonic and chromatic; incompatible cardinalities require Fit Scale or a different scale selection. For source non-scale notes, default Chromatic Policy is Snap to Source Scale, then map; optional Preserve Alteration may produce target non-scale tones and must disable a strict-scale guarantee.

Fit Scale uses deterministic nearest-pitch selection, ties downward. Preserve onset/duration, but show changed-note and pitch-collision counts. Scale fitting does not guarantee the original chord quality. Offer audition and immediate undo; never claim this is automatic harmonic correction. Optional chord-aware revoicing remains a later feature.

Drum mode bypasses every pitch transform unless the user explicitly enables Drum Transpose. Channel 10 is a useful clue, not a definitive instrument classification.

Examples using MIDI note numbers:
- Source C major [60,64,67] -> D Transpose = [62,66,69], retaining a major triad.
- Source C major [60,64,67] -> D natural minor Degree Map = [62,65,69], producing D minor.
- Trigger anchor 60, incoming 64, Transpose mode = +4 semitones relative to the configured base target.

### Live input behaviour

Choose one input role per lane/control zone:
- Transpose Root: an incoming note selects the target root; it is consumed as a control note.
- Trigger Clip: starts/retriggers the selected clip, optionally with transposition.
- Chord Follow: reserved for later chord-aware mapping; ordinary file playback must not imply automatic chord recognition.

Input filter: channel and note range. One root controller per lane in v1; last-note priority, with a held-note stack that returns to the prior held note on release. Latch retains the final selection; unlatched returns to the configured base target. Root-follow can be shared across all four lanes.

Anchor is an explicit MIDI note, default 60. In Transpose mode, total shift = configured base semitone shift + input-note minus anchor + octave offset. In Degree Map/Fit Scale, the input offset moves the target tonic; do not apply that shift twice. Root-only option folds input to pitch class; full-note option follows its octave as well.

Trigger modes: Gate (while held), Latch (continues after release) and One Shot (one clip pass). Loop is separately selectable; One Shot disables Loop. Retrigger choice: restart or continue current phase. Default launch/change quantisation: next beat; options immediate, next bar or next loop.

For key/input changes, default behaviour applies the new mapping to new attacks while existing notes finish at their original mapped pitch. Optional Cut at Boundary releases old voices before applying the new key; use this for strict no-overlap harmony changes. Never recalculate a note-off using the newly selected key.

### Timing and routing

- Host Sync is the v1 clock source. Convert source PPQ to rational quarter-note positions; preserve relative event timing.
- File tempo is displayed but does not change MPC tempo. Tempo changes embedded in a file are ignored for host-synced beat timing and flagged in the UI.
- Playback speed: half / normal / double; scale onset and duration equally.
- Loop start/end stored in beats; retain leading silence by default with an explicit Trim Start option.
- Clip launch is quantised without blocking playback or loading in the callback.
- At loop end, default Cut Tails releases notes and sustain before restarting; tail carry is deferred.
- Seek starts from the new position without chasing pre-seek note-ons in v1; restore relevant sustain state before subsequent notes and label this behaviour.
- Every lane has output channel 1–16 independent of file/input channels. Rewrite supported channel messages consistently, including note-offs and CC64.
- Default A/B/C/D channels are 1/2/3/4. If destinations share a port/channel, show a collision warning: sustain and note ownership are channel-wide at the receiver. Recommend unique routes.
- Flush notes on the old destination before changing output channel/port; then resume on the new route.
- ALSA routing feasibility remains subject to the Live 2 gate in section 3.

Track each source note instance through its actual emitted destination pitch/channel. Pair overlapping same-pitch source notes consistently in source event order. When scale fitting maps different pitches to one output note, use explicit ownership/reference counts so one release cannot prematurely stop another voice. Define coalescing/retrigger policy, expose collisions and test it with sustain. Queue limits must preserve releases.

### Clip controls and Q-Links

Notes > Browser: result list, source-part selector, Preview, Load to A/B/C/D, favourites and metadata.
Notes > Clip: loaded file, loop/trigger state, source and target key/scale, transform, output channel and timing.
Keep existing top-level tabs; use short nested names Browser and Clip.

| Bank | Knob 1 | Knob 2 | Knob 3 | Knob 4 |
|---|---|---|---|---|
| Browse | Result index | Source part | Preview velocity scale | Load destination |
| Pitch | Target root | Target scale | Octave | Transform |
| Play | Speed | Loop start | Loop end | Launch quantisation |
| Route | Input channel | Anchor note | Output channel | Input mode |

Load and Preview remain explicit buttons, not side effects of turning Browse. Source key/scale use selectors on the Clip page. Persistent per-lane controls get stable parameter identities; browser cursors and load actions are not automatable in v1.

### Persistence and acceptance

Store each loaded, normalised clip's musical event data in versioned project chunk state, subject to aggregate memory limits, plus source filename/hash, source-part selection and transformations. This keeps project recall working when removable media is absent. External paths alone are insufficient. Browser caches/favourites remain local; no user's MIDI collection is committed to git.

Required tests before shipping:
1. Format 0/1, different PPQ values, running status, polyphonic chords and sustain.
2. Exact major-to-minor degree mapping examples and interval-preserving transposition.
3. Key metadata conflicts, ambiguous estimated keys and explicit user override.
4. Input-root changes during held notes; every release uses the originally emitted pitch.
5. Scale collisions, repeated same-pitch events and sustain without stuck or prematurely cut notes.
6. Independent file/input/output channels, including output changes mid-phrase.
7. Loop/stop/seek/retrigger, tempo changes and host time-signature changes.
8. Preview stop/replace, failed loads and disconnected media.
9. Malformed/oversized files produce bounded errors while audio playback remains responsive.
10. Save/reload restores the actual clip and transformation without requiring its original path.

Current status: specification only. First implementation slice after routing is one-file, one-lane PPQ playback with channel remap and correct note lifecycle; then live transpose, scale mapping, browser and four-lane operation.


## 15. Rhythmic file-player implementation checkpoint

Added host-tested source under `ports/quadweave/`: four independent host-beat players with straight speed divisions and ratios including 3:2, 4:3, 5:4 and 7:4; Forward, Reverse and Ping-pong directions; MIDI and JSON `.progression` browsing/loading; key/scale/input transpose and output-channel remapping. Progression chords receive an explicit chosen duration at load, since the supported schema supplies notes without performance timing. The implementation README describes limits and unimplemented design items. ARM and Live 2 tests remain open.
