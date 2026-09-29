#!/usr/bin/env python3
"""Build catalog.json from the registry (catalog/plugins/*.json) and the plugins' GitHub releases.

  tools/catalog_build.py [--registry catalog/plugins] [--out catalog/dist] [--cache catalog/.cache]
                         [--yanked catalog/yanked.json] [--keep 10] [--check-registry]

For every registry entry: list the repo's releases, download the asset matching asset_pattern, validate it with
tools/catalog_check.py (--catalog, id and repo must match the entry), and record it. A release that fails is left out
and reported in <out>/problems.json (the previous good versions stay listed). Downloads are cached by asset id.
GITHUB_TOKEN (optional) raises the API rate limit. --check-registry only validates the registry files (for PRs).
Standard library only. See docs/CATALOG.md and docs/CATALOG_SPEC.md.
"""
import argparse
import datetime
import fnmatch
import glob
import json
import os
import re
import sys
import urllib.error
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import catalog_check  # noqa: E402

ID = re.compile(r"[a-z0-9]+(-[a-z0-9]+)*")
REPO = re.compile(r"[\w.-]+/[\w.-]+")
KINDS = ("instrument", "effect")
# SPDX ids accepted as open source; anything else needs a human decision (edit this list in the PR that adds it).
OPEN_LICENSES = {"MIT", "BSD-2-Clause", "BSD-3-Clause", "ISC", "Apache-2.0", "GPL-2.0-only", "GPL-2.0-or-later",
                 "GPL-3.0-only", "GPL-3.0-or-later", "LGPL-2.1-only", "LGPL-2.1-or-later", "LGPL-3.0-only",
                 "LGPL-3.0-or-later", "AGPL-3.0-only", "AGPL-3.0-or-later", "MPL-2.0", "Unlicense", "CC0-1.0", "Zlib"}


def check_entry(e, fname=None):
    """Problems with one registry entry (a list of strings)."""
    p = []
    for k in ("id", "name", "author", "repo", "kind", "license", "summary"):
        if not isinstance(e.get(k), str) or not e[k].strip():
            p.append("missing " + k)
    if p:
        return p
    if not ID.fullmatch(e["id"]):
        p.append("bad id %r" % e["id"])
    if fname and os.path.splitext(os.path.basename(fname))[0] != e["id"]:
        p.append("file name must be <id>.json")
    if not REPO.fullmatch(e["repo"]):
        p.append("repo must be owner/name")
    if e["kind"] not in KINDS:
        p.append("kind must be one of %s" % ", ".join(KINDS))
    if e["license"] not in OPEN_LICENSES and e.get("source_available") is not True:
        p.append("license %r is not on the open-source list: set \"source_available\": true if the source is public "
                 "but the license limits use (shown as a badge), see docs/CATALOG.md" % e["license"])
    if "source_available" in e and not isinstance(e["source_available"], bool):
        p.append("source_available must be true or false")
    if "style" in e and not (isinstance(e["style"], str) and ID.fullmatch(e["style"])):
        p.append("style must be a lowercase slug, e.g. sampler, synth, reverb")
    if "tags" in e and not (isinstance(e["tags"], list) and all(isinstance(t, str) and ID.fullmatch(t) for t in e["tags"])):
        p.append("tags must be a list of lowercase slugs")
    return p


def load_registry(path):
    entries, problems, ids, repos = [], [], {}, {}
    for f in sorted(glob.glob(os.path.join(path, "*.json"))):
        try:
            e = json.load(open(f))
        except ValueError as ex:
            problems.append((f, "invalid JSON: %s" % ex))
            continue
        errs = check_entry(e, f)
        if not errs:
            if e["id"] in ids:
                errs.append("duplicate id, also in " + ids[e["id"]])
            if e["repo"].lower() in repos:
                errs.append("repo already listed as " + repos[e["repo"].lower()])
        for x in errs:
            problems.append((f, x))
        if not errs:
            ids[e["id"]], repos[e["repo"].lower()] = f, e["id"]
            entries.append(e)
    return entries, problems


class GitHub:
    """Release listing and asset download over the GitHub API. Tests substitute a fake with the same two methods."""
    def __init__(self, token=None):
        self.headers = {"Accept": "application/vnd.github+json", "User-Agent": "mpc-vst-catalog"}
        if token:
            self.headers["Authorization"] = "Bearer " + token

    def list_releases(self, repo):
        out, page = [], 1
        while True:
            req = urllib.request.Request("https://api.github.com/repos/%s/releases?per_page=100&page=%d" % (repo, page),
                                         headers=self.headers)
            with urllib.request.urlopen(req, timeout=60) as r:
                chunk = json.load(r)
            out += chunk
            if len(chunk) < 100:
                return out
            page += 1

    def tested(self, repo):
        """Optional tested.json at the root of the repo's default branch: [{version, device, firmware, date}]."""
        req = urllib.request.Request("https://raw.githubusercontent.com/%s/HEAD/tested.json" % repo,
                                     headers={"User-Agent": "mpc-vst-catalog"})
        try:
            with urllib.request.urlopen(req, timeout=60) as r:
                return json.load(r)
        except urllib.error.HTTPError as ex:
            if ex.code == 404:
                return []
            raise

    def download(self, asset, dest):
        req = urllib.request.Request(asset["browser_download_url"], headers={"User-Agent": "mpc-vst-catalog"})
        with urllib.request.urlopen(req, timeout=300) as r, open(dest + ".part", "wb") as f:
            while True:
                b = r.read(1 << 20)
                if not b:
                    break
                f.write(b)
        os.replace(dest + ".part", dest)


def build(entries, src, cache, yanked, keep=10, now=None):
    """-> (catalog dict, problems list [{id, tag, error}])."""
    os.makedirs(cache, exist_ok=True)
    plugins, problems = [], []
    for e in entries:
        versions = []
        try:
            releases = src.list_releases(e["repo"])
        except Exception as ex:  # a repo we can't read: keep going, report it
            problems.append({"id": e["id"], "tag": None, "error": "cannot list releases: %s" % ex})
            releases = []
        tested = []
        if hasattr(src, "tested"):
            try:
                tested = [t for t in src.tested(e["repo"]) if isinstance(t, dict) and t.get("version") and t.get("device")]
            except Exception as ex:  # optional file: a bad one only costs the badges
                problems.append({"id": e["id"], "tag": None, "error": "tested.json ignored: %s" % ex})
        for rel in releases:
            if rel.get("draft"):
                continue
            tag = rel.get("tag_name")
            assets = [a for a in rel.get("assets", []) if fnmatch.fnmatch(a["name"], e.get("asset_pattern", "*-mpc-armv7.zip"))]
            if len(assets) != 1:
                problems.append({"id": e["id"], "tag": tag, "error": "expected one asset matching the pattern, found %d" % len(assets)})
                continue
            asset = assets[0]
            zpath = os.path.join(cache, "%s-%s.zip" % (e["id"], asset["id"]))
            try:
                if not os.path.exists(zpath):
                    src.download(asset, zpath)
                errors, warnings, rec = catalog_check.check(zpath, catalog=True, expect_id=e["id"], expect_repo=e["repo"])
            except Exception as ex:
                problems.append({"id": e["id"], "tag": tag, "error": "download/validate failed: %s" % ex})
                continue
            if errors:
                problems.append({"id": e["id"], "tag": tag, "error": "; ".join(errors)})
                continue
            if any(v["version"] == rec["version"] for v in versions):
                problems.append({"id": e["id"], "tag": tag, "error": "duplicate version %s" % rec["version"]})
                continue
            rec.update({
                "url": asset["browser_download_url"],
                "date": (rel.get("published_at") or "")[:10],
                "channel": "beta" if rel.get("prerelease") else "stable",
                "notes": rel.get("body") or "",
                "yanked": "%s@%s" % (e["id"], rec["version"]) in yanked or "%s@%s" % (e["id"], "*") in yanked,
                "warnings": warnings,
                "downloads": asset.get("download_count", 0),
            })
            rec["tested"] = [{k: t.get(k, "") for k in ("device", "firmware", "date")} for t in tested
                             if str(t["version"]).lstrip("v") == rec["version"]]
            versions.append(rec)
        vkey = lambda v: tuple(int(x) for x in v["version"].split("."))
        versions.sort(key=vkey, reverse=True)
        versions = versions[:keep]
        item = {k: e[k] for k in ("id", "name", "author", "repo", "kind", "license", "summary") }
        for k in ("screenshot", "homepage", "style"):
            if e.get(k):
                item[k] = e[k]
        item["tags"] = e.get("tags", [])
        item["source_available"] = bool(e.get("source_available"))
        item["versions"] = versions
        item["latest"] = next((v["version"] for v in versions if v["channel"] == "stable" and not v["yanked"]), None)
        item["latest_beta"] = next((v["version"] for v in versions if v["channel"] == "beta" and not v["yanked"]), None)
        item["downloads"] = sum(v["downloads"] for v in versions)
        item["updated"] = max((v["date"] for v in versions if not v["yanked"]), default="")
        plugins.append(item)
    plugins.sort(key=lambda p: p["name"].lower())
    catalog = {"schema": 1, "generated": (now or datetime.datetime.now(datetime.timezone.utc)).strftime("%Y-%m-%dT%H:%M:%SZ"),
               "plugins": plugins}
    return catalog, problems


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--registry", default="catalog/plugins")
    ap.add_argument("--out", default="catalog/dist")
    ap.add_argument("--cache", default="catalog/.cache")
    ap.add_argument("--yanked", default="catalog/yanked.json")
    ap.add_argument("--keep", type=int, default=10)
    ap.add_argument("--check-registry", action="store_true")
    a = ap.parse_args()
    entries, reg_problems = load_registry(a.registry)
    for f, m in reg_problems:
        print("error: %s: %s" % (f, m), file=sys.stderr)
    if a.check_registry:
        print("%d entries, %d problems" % (len(entries), len(reg_problems)))
        sys.exit(1 if reg_problems else 0)
    yanked = set(json.load(open(a.yanked))) if os.path.exists(a.yanked) else set()
    catalog, problems = build(entries, GitHub(os.environ.get("GITHUB_TOKEN")), a.cache, yanked, a.keep)
    os.makedirs(a.out, exist_ok=True)
    json.dump(catalog, open(os.path.join(a.out, "catalog.json"), "w"), indent=1)
    json.dump(problems, open(os.path.join(a.out, "problems.json"), "w"), indent=1)
    for p in problems:
        print("problem: %(id)s %(tag)s: %(error)s" % p, file=sys.stderr)
    print("%d plugins, %d versions, %d problems" % (len(catalog["plugins"]), sum(len(p["versions"]) for p in catalog["plugins"]), len(problems)))
    sys.exit(1 if reg_problems else 0)


if __name__ == "__main__":
    main()
