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
     "style": "sampler", "tags": ["rompler"], "screenshot": "optional URL", "asset_pattern": "*-mpc-armv7.zip" }
   ```
   `style` and `tags` (lowercase slugs) feed the site's Style filter and search; pick a short, common word such as
   `synth`, `sampler`, `drum-machine`, `reverb`, `delay`, `utility`. If your license is not on the open-source list but
   the source is public, add `"source_available": true`: the plugin is listed with a "Restricted use" badge and
   its own license text shown.
3. CI checks the entry and your latest release. Once merged, new releases appear automatically (nightly, or
   run the "Catalog build" workflow).

Open-source licenses (list in `tools/catalog_build.py`) or public source with `source_available` set. No versions or
checksums go in the entry.

## Report what you tested on
Optional `tested.json` at the root of your repo's default branch; the nightly build shows it as "Tested on" for the
matching release:
```json
[ { "version": "1.2.0", "device": "MPC Live II", "firmware": "3.6.0", "date": "2026-09-29" } ]
```

## Feed
The site publishes `feed.xml` (Atom, newest 50 non-yanked releases).

## Yank a release
Add `"<id>@<version>"` (or `"<id>@*"` for all versions) to `yanked.json`. It stays in the catalog marked yanked and is
never offered as the latest.

## Build locally
`python3 tools/catalog_build.py --check-registry` validates entries; without the flag it fetches releases (set
`GITHUB_TOKEN` to avoid API limits) and writes `catalog/dist/catalog.json` and `problems.json`.
`python3 tools/catalog_site.py` then writes the site to `catalog/dist/site/` (open `index.html`).
