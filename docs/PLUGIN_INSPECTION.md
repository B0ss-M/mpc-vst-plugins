# Inspect third-party Linux plugins for MPC

`tools/inspect_plugin.py` performs static checks for this repository's ARM32 little-endian hard-float VST2 target. It never loads the plugin, calls its entry points, runs `ldd`, installs it or modifies the device. It requires Python 3.11+ and GNU binutils (`readelf`) on Linux/WSL.

## Usage

Unpack the plugin download, then run from the kit root:

```sh
python3 tools/inspect_plugin.py /path/to/plugin.so
python3 tools/inspect_plugin.py /path/to/plugins --glibc 2.39 --json report.json
python3 tools/inspect_plugin.py /path/to/plugins --glibc 2.39 --target-libs target-libraries.txt --json report.json
```

Use the actual device's GLIBC version; 2.39 is an example, not a universal MPC guarantee. `target-libraries.txt` is a plain text inventory of library filenames or absolute paths, one per line, from the target system and any intended bundled libraries. This inventory only checks direct dependency names. It does not prove their architecture, symbol versions, search-path resolution or transitive dependencies.

Directories are scanned recursively for `.so`, versioned `.so.*` and `.clap` files, including binaries inside unpacked `.vst3` and `.lv2` bundles. Archives and installers are not unpacked or executed. Inspect only downloaded files you intend to assess. Reports may contain your local file paths.

## Checks and verdicts

The report includes ELF class, CPU, byte order, type, ARM attributes/hard-float evidence, exported plugin entry points, direct dependencies, RPATH/RUNPATH, required GLIBC/GLIBCXX/CXXABI versions, undefined dynamic symbols and SHA-256. Undefined symbols are normal for dynamically linked libraries; their presence alone is not a link failure. VST2, VST3, LV2, LADSPA and CLAP are identified by known exports. A legacy `main` export is ambiguous and needs further review.

- `REBUILD_OR_ADAPT_REQUIRED`: a definite mismatch or missing required library name was found.
- `CANDIDATE_REQUIRES_VERIFICATION`: no checked blocker found; compatibility is not established.
- `INSPECTION_ERROR`: unreadable, invalid, unsupported or timed-out input.

Exit codes: 0 = no checked blockers, 1 = at least one blocker, 2 = at least one inspection error. Unknowns remain possible with exit 0. CLI misuse also returns 2. Missing ARM attributes are treated as unknown, not proof of soft-float. The script does not derive every required instruction set from machine code.

## LinuxDAW selection

https://linuxdaw.org/ is a discovery catalog: use its format and FOSS filters, license information and upstream/source links to find candidates. Listing a Linux format does not establish an ARM build or MPC support. The `/user` page could not be retrieved during this implementation; no account-specific content was inspected. The script scans actual local binaries, not website descriptions, and does not scrape/download catalog entries.

For each candidate, record the official upstream source revision and license, then inspect its actual release binary. x86/x64 or ARM64 builds require rebuilding for this target. Other plugin formats need a VST2 engine port or a separately implemented host adapter. The script diagnoses these paths; it does not convert processor instructions or generate a finished adapter.

## Remaining gates

Check source/license and supported architecture upstream; audit dependencies against the target root filesystem; test loading in an isolated host, controls, MIDI/audio, multiple instances, preset/chunk restoration and shutdown. Build/preview an MPC skin, benchmark actual hardware and record model/firmware/date. Follow AGENTS and PORTING before deployment. Desktop GUI dependencies are flagged as warnings because some plugins work headlessly and some require them even without opening an editor.
