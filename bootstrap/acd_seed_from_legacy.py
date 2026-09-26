#!/usr/bin/env python3
"""Take the SDKs and emulator images that have no download from an existing tree.

MCC, regex, reqtools, Roadshow, AmiSSL, SQLite3 and PosixLib were collected by
hand over the years and have no stable upstream URL, so they cannot be fetched.
The same goes for Kickstart ROMs and Workbench images, which are licensed and
must not be redistributed.  This script copies them out of a previous
~/Dev/amiga installation (or wherever you point it) into the new tree.

It only ever reads from the source and only writes into toolchain/ and var/.

    acd_seed_from_legacy.py [--from ~/Dev/amiga] [--force] [--dry-run]
"""

import argparse
import shutil
from pathlib import Path

from acd_common import (
    EMU, TOOLCHAIN_SDK, ensure_dirs, info, ok, rel, step, warn,
)

DEFAULT_SOURCES = [
    Path.home() / "Dev" / "amiga",
    Path("/c/Dev/amiga"),
    Path("C:/Dev/amiga"),
]

#: SDKs with no authoritative download.  NDK 3.2 and MUI 3.8 are deliberately
#: absent: acd_install_sdk.py builds those from the fetched archives so they
#: stay reproducible.
SEED_SDKS = [
    ("MCC", "MUI custom classes: NList, NListtree, TextEditor, BetterString, ..."),
    ("regex", "regex.library headers and FD"),
    ("reqtools", "reqtools.library headers and FD"),
    ("Roadshow", "TCP/IP stack headers (bsdsocket, netinclude)"),
    ("AmiSSL-v5", "TLS/SSL headers and AmigaOS3 link libraries"),
    ("SQLite3", "SQLite3 headers and library"),
    ("PosixLib", "POSIX compatibility headers"),
]

#: Directories inside a seeded SDK that are pure ballast on a dev host.
SEED_PRUNE = {
    "Roadshow": ["source_code"],
}

EMU_ASSETS = [
    ("kickstarts", "kickstarts", ["*.rom"]),
    ("hdf", "hdf", ["workbench*.hdf"]),
    ("adf", "adf", ["*.adf"]),
]


def find_source(explicit: str | None) -> Path | None:
    if explicit:
        p = Path(explicit).expanduser()
        return p if p.is_dir() else None
    for cand in DEFAULT_SOURCES:
        if (cand / "sdk").is_dir() or (cand / "kickstarts").is_dir():
            return cand
    return None


def copy_tree(src: Path, dst: Path, prune: list[str], dry: bool) -> int:
    if dry:
        return sum(1 for _ in src.rglob("*") if _.is_file())
    if dst.exists():
        shutil.rmtree(dst)
    ensure_dirs(dst.parent)
    shutil.copytree(src, dst, symlinks=True,
                    ignore=shutil.ignore_patterns(".DS_Store", "*.uaem"))
    for victim_name in prune:
        victim = dst / victim_name
        if victim.exists():
            shutil.rmtree(victim)
            info(f"{'':14s} pruned {victim_name}")
    return sum(1 for _ in dst.rglob("*") if _.is_file())


def seed_sdks(source: Path, force: bool, dry: bool) -> None:
    src_root = source / "sdk"
    if not src_root.is_dir():
        warn(f"no sdk/ under {source} -- nothing to seed")
        return
    for name, desc in SEED_SDKS:
        src = src_root / name
        dst = TOOLCHAIN_SDK / name
        if not src.is_dir():
            warn(f"{name:14s} not present in {rel(src_root)}")
            continue
        if dst.exists() and not force:
            info(f"{name:14s} already installed")
            continue
        n = copy_tree(src, dst, SEED_PRUNE.get(name, []), dry)
        ok(f"{name:14s} {n:,} files  ({desc})")


def seed_emulator(source: Path, force: bool, dry: bool) -> None:
    for src_name, dst_name, patterns in EMU_ASSETS:
        src = source / src_name
        dst = EMU / dst_name
        if not src.is_dir():
            warn(f"{src_name:14s} not present in {rel(source)}")
            continue
        ensure_dirs(dst)
        copied = 0
        for pattern in patterns:
            for f in sorted(src.glob(pattern)):
                target = dst / f.name
                if target.exists() and not force:
                    continue
                if not dry:
                    shutil.copy2(f, target)
                copied += 1
        if copied:
            ok(f"{src_name:14s} {copied} file(s) -> {rel(dst)}")
        else:
            info(f"{src_name:14s} nothing new")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--from", dest="source", help="existing toolchain tree to copy from")
    ap.add_argument("--force", action="store_true", help="overwrite what is already there")
    ap.add_argument("--dry-run", action="store_true", help="report only, copy nothing")
    args = ap.parse_args()

    source = find_source(args.source)
    if not source:
        warn("no previous installation found")
        info("looked in: " + ", ".join(str(p) for p in DEFAULT_SOURCES))
        info("point at one with --from, or install these SDKs by hand;")
        info("see emulator/images/README.md for the emulator assets.")
        return 1

    step(f"Seeding from {source}" + ("  (dry run)" if args.dry_run else ""))
    seed_sdks(source, args.force, args.dry_run)

    step("Seeding emulator assets")
    seed_emulator(source, args.force, args.dry_run)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
