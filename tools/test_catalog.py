#!/usr/bin/env python3
"""Offline test of the release manifest and validator: builds a fake package with tools/release.py and checks that
tools/catalog_check.py accepts it and rejects tampered copies. No device, no toolchain: python3 tools/test_catalog.py"""
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import catalog_check  # noqa: E402

ENTRY = ('<PLUGIN name="Test Synth" format="VST" category="Synth" manufacturer="Acme" version="1.0" '
         'file="/sdcard/vst/test_synth.so" uid="1a2b3c4d" isInstrument="1" fileTime="0" infoUpdateTime="0" '
         'numInputs="0" numOutputs="2" isShell="0" hasARAExtension="0" uniqueId="0"/>')


def fake_so(path, machine=40, glibc=b"GLIBC_2.30"):
    hdr = bytearray(b"\x7fELF" + bytes(16))
    hdr[18:20] = machine.to_bytes(2, "little")
    open(path, "wb").write(bytes(hdr) + b"\0" + glibc + b"\0")


class Base(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, self.tmp)

    def build(self, version="1.2.0", machine=40, glibc=b"GLIBC_2.30", extra=()):
        t = self.tmp
        fake_so(os.path.join(t, "test_synth.so"), machine, glibc)
        skin = os.path.join(t, "Acme - VST - Test Synth")
        os.makedirs(os.path.join(skin, "Plugin Skins"), exist_ok=True)
        open(os.path.join(skin, "version.xml"), "w").write("<v/>")
        open(os.path.join(skin, "Plugin Skins", "TUI.json"), "w").write("{}")
        open(os.path.join(t, "entry.xml"), "w").write(ENTRY)
        out = os.path.join(t, "dist")
        subprocess.check_call([sys.executable, os.path.join(HERE, "release.py"), "--so", os.path.join(t, "test_synth.so"),
                               "--skin", skin, "--entry", os.path.join(t, "entry.xml"), "--version", version,
                               "--repo", "acme/test-synth", "--license", "MIT", "-o", out, *extra],
                              stdout=subprocess.DEVNULL)
        return os.path.join(out, "Test-Synth-%s-mpc-armv7.zip" % version)

    def tamper(self, zpath, member_suffix, fn):
        out = zpath + ".t.zip"
        with zipfile.ZipFile(zpath) as zin, zipfile.ZipFile(out, "w") as zout:
            for i in zin.infolist():
                data = zin.read(i.filename)
                if i.filename.endswith(member_suffix):
                    data = fn(data)
                zout.writestr(i, data)
        return out


class CatalogTest(Base):
    def test_good_package(self):
        z = self.build()
        errors, warnings, rec = catalog_check.check(z, catalog=True, expect_id="test-synth", expect_repo="acme/test-synth")
        self.assertEqual(errors, [])
        self.assertEqual(warnings, [])
        m = rec["manifest"]
        self.assertEqual((m["id"], m["arch"], m["max_glibc"], m["param_compat"]), ("test-synth", "armv7", "2.30", 1))
        self.assertEqual(len(rec["sha256"]), 64)

    def test_wrong_arch_and_glibc(self):
        e, _, _ = catalog_check.check(self.build(machine=62))
        self.assertTrue(any("armv7" in x for x in e))
        e, _, _ = catalog_check.check(self.build(glibc=b"GLIBC_2.38"))
        self.assertTrue(any("GLIBC" in x for x in e))

    def test_tampered_file_fails_checksum(self):
        z = self.tamper(self.build(), "payload/vst/test_synth.so", lambda d: d + b"x")
        e, _, _ = catalog_check.check(z)
        self.assertTrue(any("checksum mismatch" in x for x in e))

    def test_modified_installer_warns(self):
        z = self.build()
        # rebuild SHA256SUMS consistently so only the installer-template check can object
        import hashlib
        def mod(d): return d + b"\n# evil\n"
        z = self.tamper(z, "/install.sh", mod)
        z = self.tamper(z, "/SHA256SUMS", lambda d: "\n".join(
            l if "install.sh" not in l or "uninstall" in l else
            "%s  install.sh" % hashlib.sha256(self._install(zipfile.ZipFile(z))).hexdigest()
            for l in d.decode().splitlines()).encode() + b"\n")
        e, w, _ = catalog_check.check(z)
        self.assertEqual(e, [])
        self.assertTrue(any("install.sh differs" in x for x in w))

    @staticmethod
    def _install(zf):
        return [zf.read(n) for n in zf.namelist() if n.endswith("/install.sh")][0]

    def test_catalog_needs_repo_and_license(self):
        t = self.tmp
        z = self.build()
        z2 = self.tamper(z, "mpc-plugin.json", lambda d: json.dumps({**json.loads(d), "license": None}).encode())
        e, _, _ = catalog_check.check(z2, catalog=True)
        self.assertTrue(any("license" in x for x in e))

    def test_id_mismatch_with_registry(self):
        e, _, _ = catalog_check.check(self.build(), expect_id="other")
        self.assertTrue(any("registry id" in x for x in e))



import catalog_build  # noqa: E402


class FakeGitHub:
    def __init__(self, releases, zips):
        self.releases, self.zips = releases, zips

    def list_releases(self, repo):
        if repo not in self.releases:
            raise RuntimeError("404")
        return self.releases[repo]

    def download(self, asset, dest):
        shutil.copy(self.zips[asset["id"]], dest)


class BuildTest(Base):
    ENTRY = {"id": "test-synth", "name": "Test Synth", "author": "A", "repo": "acme/test-synth", "kind": "instrument",
             "license": "MIT", "summary": "s"}

    def rel(self, tag, aid, pre=False, name="x-mpc-armv7.zip"):
        return {"tag_name": tag, "prerelease": pre, "draft": False, "published_at": "2026-09-29T00:00:00Z", "body": "notes",
                "assets": [{"id": aid, "name": name, "browser_download_url": "https://x/" + name, "download_count": 3}]}

    def test_build_keeps_good_versions_and_reports_bad(self):
        good, newer = self.build("1.0.0"), self.build("1.1.0")
        bad = self.tamper(self.build("1.2.0"), "payload/vst/test_synth.so", lambda d: d + b"x")
        gh = FakeGitHub({"acme/test-synth": [self.rel("v1.2.0", 3), self.rel("v1.1.0", 2), self.rel("v1.0.0", 1),
                                              self.rel("v1.3.0-b", 4, pre=True, name="nope.txt")]},
                        {1: good, 2: newer, 3: bad})
        cat, problems = catalog_build.build([self.ENTRY], gh, os.path.join(self.tmp, "cache"), {"test-synth@1.0.0"})
        p = cat["plugins"][0]
        self.assertEqual([v["version"] for v in p["versions"]], ["1.1.0", "1.0.0"])
        self.assertEqual(p["latest"], "1.1.0")
        self.assertTrue(p["versions"][1]["yanked"])
        self.assertEqual(p["downloads"], 6)
        self.assertEqual(sorted((x["tag"] for x in problems)), ["v1.2.0", "v1.3.0-b"])

    def test_unreadable_repo_is_reported_not_fatal(self):
        cat, problems = catalog_build.build([self.ENTRY], FakeGitHub({}, {}), os.path.join(self.tmp, "c"), set())
        self.assertEqual(cat["plugins"][0]["versions"], [])
        self.assertIsNone(cat["plugins"][0]["latest"])
        self.assertIn("cannot list", problems[0]["error"])

    def test_registry_rules(self):
        self.assertEqual(catalog_build.check_entry(self.ENTRY, "x/test-synth.json"), [])
        self.assertTrue(catalog_build.check_entry({**self.ENTRY, "license": "Proprietary"}))
        self.assertTrue(catalog_build.check_entry(self.ENTRY, "x/other.json"))
        self.assertTrue(catalog_build.check_entry({**self.ENTRY, "repo": "nope"}))


import catalog_site  # noqa: E402


class SiteTest(unittest.TestCase):
    def test_render_embeds_catalog_safely(self):
        cat = {"schema": 1, "generated": "x", "plugins": [{"id": "a", "name": "A </script><b>", "versions": []}]}
        html = catalog_site.render(cat)
        self.assertNotIn("/*CATALOG_JSON*/", html)
        data = html.split('<script id="data" type="application/json">')[1].split("</script>")[0]
        self.assertEqual(json.loads(data), cat)   # round-trips, and the embedded "</script>" can't end the block

    def test_registry_style_and_source_available(self):
        e = dict(BuildTest.ENTRY, license="MAME license")
        self.assertTrue(catalog_build.check_entry(e))
        self.assertEqual(catalog_build.check_entry(dict(e, source_available=True, style="rompler", tags=["jv-880"])), [])
        self.assertTrue(catalog_build.check_entry(dict(BuildTest.ENTRY, style="Bad Style")))


if __name__ == "__main__":
    unittest.main()
