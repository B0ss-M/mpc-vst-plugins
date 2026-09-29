# Plugin catalog

Open-source MPC/Force VST plugins, with versions and download links discovered from each plugin's GitHub releases.
Design: `docs/CATALOG.md`. Formats: `docs/CATALOG_SPEC.md`.

## Add your plugin
1. Make a release zip with `tools/release.py` (or the `vst-release.yml` workflow) with `--repo owner/name --license <SPDX>`,
   and check it: `tools/catalog_check.py <zip> --catalog`. Publish it as a GitHub release on your repo.
2. Open a PR adding `catalog/plugins/<id>.json` (the `<id>` in your manifest):
   ```json
   { "id": "my-synth", "name": "My Synth", "author": "Your name", "repo": "you/my-synth-vst",
     "kind": "instrument", "license": "MIT", "summary": "One line.",
     "screenshot": "optional URL", "asset_pattern": "*-mpc-armv7.zip" }
   ```
3. CI checks the entry and your latest release. Once merged, new releases appear automatically (nightly, or
   run the "Catalog build" workflow).

Open-source licenses only (the list is in `tools/catalog_build.py`). No versions or checksums go in the entry.

## Yank a release
Add `"<id>@<version>"` (or `"<id>@*"` for all versions) to `yanked.json`. It stays in the catalog marked yanked and is
never offered as the latest.

## Build locally
`python3 tools/catalog_build.py --check-registry` validates entries; without the flag it fetches releases (set
`GITHUB_TOKEN` to avoid API limits) and writes `catalog/dist/catalog.json` and `problems.json`.
