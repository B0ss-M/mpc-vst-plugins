---
title: Add your plugin
nav: Add yours
order: 40
summary: How to get a plugin into the catalog. It takes one small file, once; after that new releases appear on their own.
---

The catalog is a list of plugins you point it at. It reads versions, links and checksums from your GitHub releases, so you never edit it for a new version.

## What can be listed
- **Open source**, or with public source and a license that limits use, such as non-commercial. Limited-use plugins carry a *Restricted use* badge.
- **A GitHub repo** with the source and a license file.
- **A release zip** built with this repo's `tools/release.py` (or its workflow) for 32-bit ARM MPC OS devices, with a version like `1.2.0`.
- **No closed binaries**, and nothing that ships Akai's own skins or files. Do not include sound content you have no right to share.

## 1. Publish a release the catalog can read
Build your zip with a repo, a license and, if you like, a catalog id, then check it locally:

```
tools/release.py ... --repo you/your-plugin-repo --license MIT --id your-plugin -o dist
tools/catalog_check.py dist/Your-Plugin-1.0.0-mpc-armv7.zip --catalog
```

Attach the zip to a GitHub release on your repo. The [release workflow](workflow.html) page has the whole path.

## 2. Add one file
Open a pull request to [mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins) adding `catalog/plugins/<id>.json`, where `<id>` is the id in your zip's manifest:

```
{ "id": "your-plugin", "name": "Your Plugin", "author": "Your name",
  "repo": "you/your-plugin-repo", "kind": "instrument", "license": "MIT",
  "summary": "One line that says what it sounds like or does.",
  "style": "sampler", "tags": ["rompler"] }
```

| Field | Notes |
|---|---|
| `id` | Lowercase letters, digits and hyphens. The file name must match. Never changes. |
| `kind` | `instrument` or `effect` |
| `license` | An SPDX id such as `MIT` or `GPL-3.0-only` |
| `source_available` | `true` if the license is not on the open-source list but the source is public. Shows the *Restricted use* badge. |
| `style`, `tags` | Optional lowercase words for the Style filter and search: `synth`, `sampler`, `drum-machine`, `reverb`, `delay`, `utility` |
| `screenshot`, `homepage` | Optional links |
| `asset_pattern` | Optional. Which release file to use. The default is `*-mpc-armv7.zip`. |

Do not put versions or checksums in this file. The catalog reads them from your release.

## 3. What happens next
- The pull request runs a check on your entry, and a maintainer merges it.
- Every night, and whenever the catalog build is run, the catalog reads your releases, downloads each zip and checks it: file layout, checksums, ARM build, the highest glibc it needs, that the installer matches the standard one, and that the id, uid and repo agree with your entry.
- A release that passes appears on the site with its date, size and checksum. GitHub prereleases show up as beta.
- A release that fails is left out, your previous good version stays, and an issue is opened on the catalog repository explaining why.

## Keeping it working
- Keep the `uid`, the `.so` name and the catalog id fixed across releases.
- Raise the major version only when parameter positions change, because that breaks saved projects.
- Add a `tested.json` to say which devices you tried. See the [release workflow](workflow.html).
- To pull a bad release, open a pull request adding `"<id>@<version>"` to `catalog/yanked.json`. It stays listed as yanked and is never offered as the latest.

## Rules of the road
This is community software that runs as root on people's devices. Say what the plugin does, keep the source public, and fix release problems quickly. Maintainers may remove a plugin that is unsafe or misleading.
