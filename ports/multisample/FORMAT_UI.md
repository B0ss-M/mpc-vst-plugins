# Format-aware interface — design draft

Two surfaces serve different jobs. The **desktop importer** recognizes a source, exposes its relevant import choices, previews the resulting note/velocity map, and reports unsupported features before writing a bundle. The **MPC VST skin** plays the normalized bundle with stable VST parameter indices; it shows source provenance and contextual information without pretending to recreate another sampler's entire editor.

The first formats are WAV/AIFF/CAF/FLAC/OGG/NCW (single sample), SFZ, SoundFont 2, Kontakt NKI, Logic EXS24, Korg KMP/KSF, and modern MPC XPM. Support is rolled out one format at a time behind fixtures. The GUI may display a format before its importer is ready, but must disable Import with an explicit "planned" status. ConvertWithMoss's documented reader support does not guarantee every proprietary feature, script, effect, or encrypted library is portable.

## Desktop import flow

1. Choose a file or folder. Detect format and show the detected subtype and source path. An explicit format override is available only when detection is ambiguous.
2. Display format-specific choices below. Choose a preset/track when the file contains more than one. Show the zone map: key range horizontally, velocity vertically, with labels for root note, sample name, loop and round-robin group. Audition is a later feature, not a prerequisite for import.
3. Validate sample references and source permissions. Show counts for zones, samples, missing files, estimated decoded size, and unsupported features. **Import** remains disabled for missing required audio or fatal structural errors; approximations are individually listed and require acknowledgement.
4. Write an instrument bundle into a staging folder, verify checksums and paths, and atomically move it into the destination. Show the bundle location and an import report that can be reopened from the MPC's bundle folder.

| Source | Context shown | Import choices | Explicit caveat |
| --- | --- | --- | --- |
| Single audio file | Waveform metadata, channels, sample rate, embedded root/loop if present | Root note, key range, one-shot or gate, optional loop | Never guess a root note without showing the choice. |
| SFZ | Region count, referenced sample paths, used/unsupported opcodes | Preset name, missing-sample search root, supported opcode subset report | Scripts and unsupported opcodes are reported, not executed. |
| SoundFont 2 | Bank/preset tree, instruments, velocity layers, stereo links | Bank and preset selection, handling for mismatched stereo pairs | Preset/global generators must be flattened exactly once. |
| Kontakt NKI | Instrument/group names, sample paths, detected scripts | Instrument selection, sample search root | KSP scripts, protected/encrypted content and effects may not transfer. |
| Logic EXS24 | Zone/group map, audio path resolution | Instrument selection, sample search root | Report unsupported modulation/routing. |
| Korg KMP/KSF | Multisample and referenced KSF file list | Multisample selection, search root | Report missing linked KSF assets and unsupported loop modes. |
| MPC XPM | Program/track name, keygroups/layers, embedded or linked samples | Program or track selection, sample search root | The importer's interpretation of MPC 2/3 versions must be fixture-tested. |

The browser prototype at `ui/format-import.html` shows these panels and validation states. It is an interactive **visual specification**, not a converter; its buttons do not write files.

## MPC skin after import

Keep Play and Shape controls in the same positions for every instrument, so Q-Link muscle memory survives patch changes. A source badge and readout row show `Imported from SFZ` (or its actual format), instrument name, zones, layers, and `Ready / Loading / Missing sample / Unsupported bundle`. The third page is **Source & Zones**: a read-only zone summary plus metadata useful for the current source, such as SFZ region/opcode summary or SF2 bank/preset. This information comes from the bundle's import report, never from parsing the original source on the device.

MPC's skin is a static JSON/PNG asset for the VST, and the VST parameter list/order is fixed. The skin can conditionally show controls using `when=<enum>:<option>`; it cannot load a different UI definition from each user file. A later wrapper extension may expose an engine-owned, read-only `source_format` enum and defer `audioMasterUpdateDisplay` to refresh text on patch loads. Until this is implemented and verified on hardware, use a common Source page with a live format readout. Format-specific *editable* controls belong in the desktop importer unless the normalized engine has a genuine matching parameter.

Do not insert new VST parameters among published ones. Append any future source/status parameters to `params.json`, keep existing indices stable, and test save/reload after changing the manifest. Avoid format-specific skins requiring a plugin reinstall or restart when a user changes a patch.

## UI acceptance checks

- Every first-wave format has one successful fixture and one missing/corrupt asset fixture. The preview's key/velocity rectangles match the emitted bundle; the import report identifies unsupported fields.
- A failed import never replaces a valid bundle. A patch switch to a missing bundle preserves the previous playable patch and shows an error.
- On MPC, switching between different source formats does not move Play Q-Links or alter saved parameter indices. Instrument name and status refresh without a second user gesture.
- Long names truncate visually without changing stored identity; every text readout has a clear label and can be read on the device at its actual resolution.
