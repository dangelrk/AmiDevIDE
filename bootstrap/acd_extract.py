#!/usr/bin/env python3
"""Archive extraction for the bootstrap: tar.gz, zip and -- the awkward one -- lha.

Amiga archives are LHA/LZH, which no platform ships a reader for by default, so
extract_lha() walks a cascade of options and only gives up (with instructions)
when none of them is available.

Usable as a library (extract(archive, dest)) or from the command line:

    acd_extract.py <archive> <destdir>
"""

import shutil
import subprocess
import sys
import tarfile
import zipfile
from pathlib import Path

from acd_common import fail, have, info, run

LHA_HINT = (
    "install one of:\n"
    "          pip3 install lhafile          (portable, no system packages)\n"
    "          brew install lhasa            (macOS)\n"
    "          sudo apt install lhasa        (Debian/Ubuntu)\n"
    "          pacman -S mingw-w64-x86_64-p7zip   (MSYS2)"
)


def _strip_common_prefix(dest: Path) -> None:
    """If everything landed in a single directory, hoist its contents up.

    Archives are inconsistent about whether they carry a top-level folder;
    callers want a predictable layout, so normalise to "contents of dest".
    """
    entries = [p for p in dest.iterdir() if p.name not in (".", "..")]
    if len(entries) != 1 or not entries[0].is_dir():
        return
    inner = entries[0]
    tmp = dest.parent / (dest.name + ".hoist")
    inner.rename(tmp)
    shutil.rmtree(dest)
    tmp.rename(dest)


def extract_tar(archive: Path, dest: Path) -> None:
    with tarfile.open(archive) as tf:
        # filter="data" refuses absolute paths and traversal (Python >= 3.12)
        try:
            tf.extractall(dest, filter="data")
        except TypeError:
            tf.extractall(dest)


def extract_zip(archive: Path, dest: Path) -> None:
    with zipfile.ZipFile(archive) as zf:
        for member in zf.infolist():
            target = (dest / member.filename).resolve()
            if not str(target).startswith(str(dest.resolve())):
                fail(f"refusing path traversal in {archive.name}: {member.filename}")
        zf.extractall(dest)


def _lhafile_extract(archive: Path, dest: Path) -> bool:
    try:
        import lhafile
    except ImportError:
        return False
    lf = lhafile.Lhafile(str(archive))
    for name in lf.namelist():
        # LHA stores AmigaDOS separators; normalise and reject traversal
        rel = name.replace("\\", "/").lstrip("/")
        if ".." in Path(rel).parts:
            fail(f"refusing path traversal in {archive.name}: {name}")
        out = dest / rel
        data = lf.read(name)
        if data is None:          # directory entry
            out.mkdir(parents=True, exist_ok=True)
            continue
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_bytes(data)
    return True


def _only_empty_dirs_failed(output: str, dest: Path) -> bool:
    """Forgive lhasa for the empty directories it cannot create.

    Some archives (mui38dev.lha) store an empty directory as an ordinary -lh0-
    entry whose name ends in "/" rather than as an -lhd- entry.  lhasa tries
    to create a file of that name, reports "Failure" and exits 1, although
    every real file was extracted.  If those are the only failures, create the
    directories ourselves and accept the result.
    """
    failed = []
    for line in output.replace("\r", "\n").splitlines():
        name, sep, status = line.rpartition("\t- ")
        if sep and status.strip() == "Failure":
            failed.append(name.strip())
    if not failed or not all(name.endswith("/") for name in failed):
        return False
    root = dest.resolve()
    for name in failed:
        path = (dest / name).resolve()
        if not path.is_relative_to(root):
            return False
        path.mkdir(parents=True, exist_ok=True)
    return True


def extract_lha(archive: Path, dest: Path) -> None:
    dest.mkdir(parents=True, exist_ok=True)
    archive = archive.resolve()

    for tool, argv in (
        ("lha", ["lha", "-xw=" + str(dest), str(archive)]),
        ("lhasa", ["lhasa", "-xw=" + str(dest), str(archive)]),
        ("7z", ["7z", "x", "-y", "-o" + str(dest), str(archive)]),
        ("7zz", ["7zz", "x", "-y", "-o" + str(dest), str(archive)]),
    ):
        if not have(tool):
            continue
        proc = subprocess.run(argv, capture_output=True, text=True)
        if proc.returncode == 0:
            info(f"extracted with {tool}")
            return
        if tool in ("lha", "lhasa") and _only_empty_dirs_failed(
                proc.stdout + proc.stderr, dest):
            info(f"extracted with {tool} (created its empty directories)")
            return
        info(f"{tool} could not read {archive.name}, trying the next option")

    if _lhafile_extract(archive, dest):
        info("extracted with the Python lhafile module")
        return

    fail(f"no working LHA extractor for {archive.name}", LHA_HINT)


def extract(archive: Path, dest: Path, strip_prefix: bool = False) -> Path:
    """Extract into dest (created if needed) and return dest."""
    archive = Path(archive)
    dest = Path(dest)
    dest.mkdir(parents=True, exist_ok=True)
    name = archive.name.lower()

    if name.endswith((".tar.gz", ".tgz", ".tar.bz2", ".tar.xz", ".tar")):
        extract_tar(archive, dest)
    elif name.endswith(".zip"):
        extract_zip(archive, dest)
    elif name.endswith((".lha", ".lzh")):
        extract_lha(archive, dest)
    else:
        fail(f"do not know how to extract {archive.name}")

    if strip_prefix:
        _strip_common_prefix(dest)
    return dest


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    extract(Path(sys.argv[1]), Path(sys.argv[2]))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
