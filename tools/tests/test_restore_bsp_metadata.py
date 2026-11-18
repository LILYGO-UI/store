from __future__ import annotations

import importlib.util
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


spec = importlib.util.spec_from_file_location(
    "restore_bsp_metadata", Path(__file__).resolve().parents[1] / "restore-bsp-metadata.py")
metadata = importlib.util.module_from_spec(spec)
spec.loader.exec_module(metadata)


@unittest.skipUnless(shutil.which("dpkg-deb"), "dpkg-deb is required")
class RestoreMetadataTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.sysroot = self.root / "sysroot"
        self.database = self.sysroot / "var/lib/dpkg"
        self.database.mkdir(parents=True)
        self.library = "usr/lib/aarch64-linux-gnu/libfixture.so.1"
        self.payload = b"\x7fELFtest fixture shared library"

    def package(self, *, package="libfixture1", version="1.0", payload=None):
        source = self.root / (package + "-" + version)
        control = source / "DEBIAN"
        control.mkdir(parents=True)
        paragraph = (f"Package: {package}\nVersion: {version}\nArchitecture: arm64\n"
                     "Maintainer: Test <test@example.com>\nMulti-Arch: same\n"
                     "Description: BSP metadata test\n")
        (control / "control").write_text(paragraph)
        (control / "shlibs").write_text(f"libfixture 1 {package} (>= {version})\n")
        script = control / "postinst"
        script.write_text("#!/bin/sh\nexit 99\n")
        script.chmod(0o755)
        library = source / self.library
        library.parent.mkdir(parents=True)
        library.write_bytes(self.payload if payload is None else payload)
        archive = source.with_suffix(".deb")
        subprocess.run(["dpkg-deb", "--build", "--root-owner-group", str(source), str(archive)],
                       check=True, capture_output=True)
        return archive, paragraph

    def install_fixture(self, paragraph):
        (self.database / "status").write_text(paragraph + "Status: install ok installed\n")
        library = self.sysroot / self.library
        library.parent.mkdir(parents=True, exist_ok=True)
        library.write_bytes(self.payload)

    def test_restores_only_metadata_and_can_repeat(self):
        archive, paragraph = self.package()
        self.install_fixture(paragraph)
        status = (self.database / "status").read_bytes()
        for _ in range(2):
            self.assertEqual(metadata.restore(self.sysroot, [archive]), 1)
        info = self.database / "info"
        self.assertEqual({p.name for p in info.iterdir()},
                         {"libfixture1:arm64.list", "libfixture1:arm64.shlibs"})
        self.assertIn("/" + self.library, (info / "libfixture1:arm64.list").read_text())
        self.assertEqual((self.database / "status").read_bytes(), status)
        self.assertEqual((self.sysroot / self.library).read_bytes(), self.payload)

    def test_rejects_version_mismatch_before_writing(self):
        archive, paragraph = self.package(version="2.0")
        self.install_fixture(paragraph.replace("Version: 2.0", "Version: 1.0"))
        with self.assertRaisesRegex(metadata.MetadataError, "Version mismatch"):
            metadata.restore(self.sysroot, [archive])
        self.assertFalse((self.database / "info").exists())

    def test_rejects_library_mismatch_before_writing(self):
        archive, paragraph = self.package(payload=b"\x7fELFdifferent library")
        self.install_fixture(paragraph)
        with self.assertRaisesRegex(metadata.MetadataError, "content mismatch"):
            metadata.restore(self.sysroot, [archive])
        self.assertFalse((self.database / "info").exists())

    def test_rejects_incomplete_batch_without_partial_metadata(self):
        archive, paragraph = self.package()
        self.install_fixture(paragraph)
        other, _ = self.package(package="libunknown1")
        with self.assertRaisesRegex(metadata.MetadataError, "not installed"):
            metadata.restore(self.sysroot, [archive, other])
        self.assertFalse((self.database / "info").exists())

    def test_preserves_existing_different_metadata(self):
        archive, paragraph = self.package()
        self.install_fixture(paragraph)
        info = self.database / "info"
        info.mkdir()
        original = info / "libfixture1:arm64.shlibs"
        original.write_text("existing metadata\n")
        with self.assertRaisesRegex(metadata.MetadataError, "existing different metadata"):
            metadata.restore(self.sysroot, [archive])
        self.assertEqual(original.read_text(), "existing metadata\n")
        self.assertFalse((info / "libfixture1:arm64.list").exists())

    def test_rejects_paths_outside_archive(self):
        for name in ("/etc/passwd", "usr/../../etc/passwd", "usr/lib\nextra"):
            with self.assertRaises(metadata.MetadataError):
                metadata.relative_member(name)


if __name__ == "__main__":
    unittest.main()
