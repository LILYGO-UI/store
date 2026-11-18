#!/usr/bin/env python3
"""Restore old BSP package metadata from matching Debian archives, offline.

Only dpkg info files are written. Library payloads, installed versions and
maintainer scripts are never installed or changed. Every archive must match
the BSP's installed package version and its existing shared-library bytes.
"""

from __future__ import annotations

import argparse
import hashlib
import io
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile


class MetadataError(RuntimeError):
    pass


def fields(paragraph: str) -> dict[str, str]:
    return dict(line.split(": ", 1) for line in paragraph.splitlines()
                if ": " in line and not line.startswith((" ", "\t")))


def archive_stream(archive: Path, flag: str) -> tarfile.TarFile:
    result = subprocess.run(["dpkg-deb", flag, str(archive)], capture_output=True)
    if result.returncode:
        raise MetadataError(f"Cannot read {archive}: {result.stderr.decode(errors='replace').strip()}")
    return tarfile.open(fileobj=io.BytesIO(result.stdout))


def relative_member(name: str) -> PurePosixPath:
    path = PurePosixPath(name)
    if path.is_absolute() or ".." in path.parts or "\n" in name or "\r" in name:
        raise MetadataError(f"Unsafe archive member: {name!r}")
    return path


def prepare_metadata(sysroot: Path, archive: Path, installed: dict) -> tuple[dict[str, bytes], int]:
    controls = {}
    with archive_stream(archive, "--ctrl-tarfile") as stream:
        for entry in stream:
            name = relative_member(entry.name).as_posix()
            if entry.isfile() and name in ("control", "symbols", "shlibs"):
                controls[name] = stream.extractfile(entry).read()
    if "control" not in controls:
        raise MetadataError(f"Missing Debian control file: {archive}")
    control = fields(controls["control"].decode())
    package, architecture = control.get("Package", ""), control.get("Architecture", "")
    if not re.fullmatch(r"[a-z0-9][a-z0-9+.-]+", package) or not re.fullmatch(r"[a-z0-9-]+", architecture):
        raise MetadataError(f"Invalid package identity: {archive}")
    current = installed.get((package, architecture))
    if not current or current.get("Status") != "install ok installed":
        raise MetadataError(f"{package}:{architecture} is not installed in this BSP")
    if control.get("Version") != current.get("Version"):
        raise MetadataError(f"Version mismatch for {package}: BSP={current.get('Version')}, "
                            f"archive={control.get('Version')}")
    if control.get("Multi-Arch") != current.get("Multi-Arch"):
        raise MetadataError(f"Multi-Arch mismatch for {package}")
    if not any(name in controls for name in ("symbols", "shlibs")):
        raise MetadataError(f"No shared-library dependency metadata in {archive}")

    paths = []
    verified = 0
    with archive_stream(archive, "--fsys-tarfile") as stream:
        for entry in stream:
            relative = relative_member(entry.name)
            paths.append("/" + relative.as_posix())
            if not entry.isfile() or ".so" not in relative.name:
                continue
            target = sysroot / relative
            if not target.is_file():
                continue
            if not target.resolve().is_relative_to(sysroot):
                raise MetadataError(f"Library escapes the BSP: {target}")
            payload = stream.extractfile(entry).read()
            if not payload.startswith(b"\x7fELF"):
                continue
            if hashlib.sha256(payload).digest() != hashlib.sha256(target.read_bytes()).digest():
                raise MetadataError(f"Library content mismatch for {package}: {relative}")
            verified += 1
    if not verified:
        raise MetadataError(f"No matching shared libraries found in this BSP for {package}")

    identity = f"{package}:{architecture}" if control.get("Multi-Arch") == "same" else package
    metadata = {f"{identity}.list": ("\n".join(paths) + "\n").encode()}
    for name in ("symbols", "shlibs"):
        if name in controls:
            metadata[f"{identity}.{name}"] = controls[name]
    return metadata, verified


def restore(sysroot: Path, archives: list[Path]) -> int:
    sysroot = sysroot.resolve()
    database = sysroot / "var/lib/dpkg"
    if sysroot == Path("/") or not database.resolve().is_relative_to(sysroot):
        raise MetadataError("Expected a separate BSP sysroot, not the host package database")
    status = database / "status"
    if not status.is_file():
        raise MetadataError(f"Missing BSP package inventory: {status}")
    installed = {}
    for paragraph in status.read_text().split("\n\n"):
        record = fields(paragraph)
        installed[(record.get("Package"), record.get("Architecture"))] = record
    metadata = {}
    verified = 0
    # Validate the complete batch before making any changes to the BSP.
    for archive in archives:
        records, count = prepare_metadata(sysroot, archive, installed)
        if metadata.keys() & records.keys():
            raise MetadataError(f"Duplicate package in archive set: {archive}")
        metadata.update(records)
        verified += count
    if not metadata:
        raise MetadataError("No Debian archives supplied")
    info = database / "info"
    if info.is_symlink():
        raise MetadataError(f"Refusing symlinked metadata directory: {info}")
    for name, content in metadata.items():
        path = info / name
        if path.is_symlink() or (path.exists() and path.read_bytes() != content):
            raise MetadataError(f"Refusing to replace existing different metadata: {path}")
    info.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".restore-", dir=database) as temporary:
        for name, content in metadata.items():
            staged = Path(temporary) / name
            staged.write_bytes(content)
            os.replace(staged, info / name)
    return verified


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sysroot", type=Path, required=True)
    parser.add_argument("--deb-dir", type=Path, required=True,
                        help="Directory containing exact-version runtime .deb files from the BSP's package sources")
    args = parser.parse_args()
    if not shutil.which("dpkg-deb"):
        parser.error("dpkg-deb is required (macOS: brew install dpkg)")
    try:
        archives = sorted(args.deb_dir.glob("*.deb"))
        verified = restore(args.sysroot, archives)
    except (MetadataError, OSError, ValueError, tarfile.TarError) as error:
        print(f"BSP metadata restoration failed: {error}", file=sys.stderr)
        return 1
    print(f"Restored metadata for {len(archives)} packages; verified {verified} shared libraries.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
