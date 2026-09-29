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


class CatalogTest(unittest.TestCase):
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


if __name__ == "__main__":
    unittest.main()
