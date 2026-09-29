# Catalog specification (schema 1)

Three formats: the **release manifest** inside every zip (written by `tools/release.py`), the **registry entry**
(written once by a plugin's author), and the generated **catalog.json**. Design and roadmap: `docs/CATALOG.md`.
Validate a zip with `tools/catalog_check.py <zip> [--catalog]`; the offline test is `python3 tools/test_catalog.py`.

## Release zip
`<Name>-<X.Y.Z>-mpc-armv7.zip`, one top folder `<Name>-<X.Y.Z>/` (layout in `docs/RELEASING.md`), containing
`mpc-plugin.json`, `plugin.xml`, `install.sh`, `uninstall.sh`, `plugin_list.awk`, `INSTALL.md`, `SHA256SUMS` and
`payload/`. Every file except `SHA256SUMS` is listed there. No absolute or `..` paths, no symlinks leaving the package.

## `mpc-plugin.json`
| field | meaning |
|---|---|
| `schema` | `1` |
| `id` | catalog id, `[a-z0-9]+(-[a-z0-9]+)*`; never changes |
| `name`, `manufacturer` | as in the plugin list entry |
| `version` | `X.Y.Z`; X bumps when parameter indices change |
| `param_compat` | equals X: a bump means saved projects change |
| `kind` | `instrument` or `effect` |
| `uid` | VST uid (hex), same as `plugin.xml`; never changes |
| `so`, `so_dir` | library file name and the directory in the plugin-list entry |
| `skin`, `extras` | skin folder name; extra payload paths under `vst/` |
| `arch` | ELF machine of the `.so`; the catalog accepts `armv7` only |
| `max_glibc` | highest `GLIBC_x.y` symbol version needed; the catalog limit is 2.36 |
| `about`, `requires` | one-line description; extra requirements |
| `source_repo`, `license` | `owner/name` on GitHub; SPDX id. **Required for the catalog** |
| `cpu` | `{p99_pct, max_pct, verdict}` from `tools/bench.sh -j`, or null |

## Validator rules (`catalog_check.py`)
Errors (exit 1): unsafe paths; missing required file; manifest missing a field or wrong schema; bad id/version;
`param_compat` != major; arch not armv7; GLIBC above 2.36; `plugin.xml` `file=`/`uid`/`name` disagree with the
manifest; `.so` not ELF; skin missing `version.xml` or `Plugin Skins/TUI.json`; a file missing from or wrong in
`SHA256SUMS`; with `--catalog`, no `source_repo` or `license`; with `--expect-id/--expect-repo`, a registry mismatch.
Warnings (need a human look): `install.sh`/`uninstall.sh`/`plugin_list.awk` differ from the repo's current template
(regenerated from the manifest and compared), `max_glibc` not recorded.

## Registry entry: `plugins/<id>.json` (catalog repo)
```json
{ "id": "my-synth", "name": "My Synth", "author": "Someone", "repo": "someone/my-synth-vst",
  "kind": "instrument", "license": "MIT", "summary": "One line.",
  "screenshot": "optional URL or path", "asset_pattern": "*-mpc-armv7.zip" }
```
No version fields: they are read from the releases. `id` must equal the manifest `id`, `repo` the manifest
`source_repo`. Stable releases are GitHub releases that are not prereleases; prereleases form the beta channel.

## `catalog.json` (generated)
`{"schema": 1, "generated": <ISO time>, "plugins": [ <registry fields> + "versions": [ <record>, ... ] ]}`, versions
newest first. A record is what `catalog_check.py --json` prints (`version`, `size`, `sha256` of the zip,
`param_compat`, `max_glibc`, `cpu`, `manifest`) plus `url`, `date`, `channel` (`stable`|`beta`), `notes`, `yanked`
and `tested` (`[{device, firmware, date}]`), added by the builder.

## Portable paths (for engines)
Engines locate their data next to the `.so` (`wrapper/plugin_dir.h`, `MODULE_SUBDIR`), never at a fixed `/sdcard`.
The installers currently still install to the directory in the entry's `file=` and skins to `/sdcard/Synths`.
