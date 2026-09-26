#!/usr/bin/env python3
"""Install the downloadable SDKs into toolchain/sdk/.

Two of the SDKs have an authoritative download (NDK 3.2 and MUI 3.8); this
script unpacks those and normalises their directory names to what the IDE's SDK
registry expects.  The rest -- MCC, regex, reqtools, Roadshow, AmiSSL, SQLite3,
PosixLib -- have no stable upstream URL and come from acd_seed_from_legacy.py.

    acd_install_sdk.py [--force]
"""

import argparse
import shutil
from pathlib import Path

from acd_common import (
    DOWNLOADS, TOOLCHAIN_SDK, TOOLCHAIN_TARGETS, VAR,
    ensure_dirs, fail, info, ok, rel, step, warn,
)
from acd_extract import extract

#: id -> (installed directory, archive root to strip, prune list)
#:
#: "archive root to strip" is the single folder some archives wrap everything
#: in; NDK 3.2 has none, MUI 3.8 wraps in MUI/.
SDKS = {
    "ndk32": {
        "dir": "NDK_3.2",
        "strip": None,
        # 12 MB of wget/tcpdump sources that no build here ever touches
        "prune": ["SANA+RoadshowTCP-IP/source_code"],
        "expect": ["Include_H", "Include_I", "lib", "FD", "SFD", "Autodocs"],
    },
    "mui38": {
        "dir": "MUI_3.8",
        "strip": "MUI",
        "prune": [],
        "expect": ["Developer/C/Include", "Developer/FD", "Developer/Autodocs"],
    },
}

#: SDKs with no download; acd_seed_from_legacy.py provides them.
SEEDED = ["MCC", "regex", "reqtools", "Roadshow", "AmiSSL-v5", "SQLite3", "PosixLib"]


def _find_archive(source_id: str, patterns: list[str]) -> Path | None:
    for pattern in patterns:
        hits = sorted(DOWNLOADS.glob(pattern))
        if hits:
            return hits[-1]
    return None


def _drop_amiga_icons(root: Path) -> int:
    """Remove Workbench .info files, which are noise on a host filesystem."""
    n = 0
    for info_file in root.rglob("*.info"):
        if info_file.is_file():
            info_file.unlink()
            n += 1
    return n


def install_sdk(source_id: str, spec: dict, force: bool) -> bool:
    target = TOOLCHAIN_SDK / spec["dir"]
    if target.exists() and not force:
        info(f"{spec['dir']:12s} already installed: {rel(target)}")
        return True

    patterns = {
        "ndk32": ["NDK3.2*.lha", "NDK_3.2*.lha"],
        "mui38": ["mui38dev*.lha"],
    }[source_id]
    archive = _find_archive(source_id, patterns)
    if not archive:
        warn(f"{spec['dir']:12s} no archive in {rel(DOWNLOADS)} -- run acd_fetch.py")
        return False

    staging = VAR / "stage" / source_id
    if staging.exists():
        shutil.rmtree(staging)
    info(f"{spec['dir']:12s} unpacking {archive.name}")
    extract(archive, staging)
    icons = _drop_amiga_icons(staging)

    root = staging
    if spec["strip"]:
        inner = staging / spec["strip"]
        if not inner.is_dir():
            fail(f"{archive.name}: expected a top-level {spec['strip']}/ directory")
        root = inner

    for pruned in spec["prune"]:
        victim = root / pruned
        if victim.exists():
            size = sum(f.stat().st_size for f in victim.rglob("*") if f.is_file())
            shutil.rmtree(victim)
            info(f"{'':12s} pruned {pruned} ({size >> 20} MB, unused sources)")

    if icons:
        info(f"{'':12s} removed {icons} Workbench .info files")

    missing = [d for d in spec["expect"] if not (root / d).exists()]
    if missing:
        fail(f"{archive.name}: unexpected layout, missing {', '.join(missing)}")

    if target.exists():
        shutil.rmtree(target)
    ensure_dirs(target.parent)
    shutil.move(str(root), str(target))
    shutil.rmtree(staging, ignore_errors=True)

    files = sum(1 for _ in target.rglob("*") if _.is_file())
    ok(f"{spec['dir']:12s} installed to {rel(target)} ({files:,} files)")
    return True


def install_vbcc_target(force: bool) -> bool:
    """The prebuilt vbcc C runtime for m68k-amigaos (headers + libs)."""
    target = TOOLCHAIN_TARGETS / "m68k-amigaos"
    if (target / "lib" / "vc.lib").exists() and not force:
        info(f"{'m68k target':12s} already installed: {rel(target)}")
        return True

    archive = _find_archive("vbcc_target_m68k", ["vbcc_target_m68k-amigaos.lha"])
    if not archive:
        warn(f"{'m68k target':12s} no archive in {rel(DOWNLOADS)} -- run acd_fetch.py")
        return False

    staging = VAR / "stage" / "vbcc_target"
    if staging.exists():
        shutil.rmtree(staging)
    info(f"{'m68k target':12s} unpacking {archive.name}")
    extract(archive, staging)
    _drop_amiga_icons(staging)

    # The archive wraps everything in vbcc_target_m68k-amigaos/, but that name
    # has changed across releases -- locate the payload rather than assume it.
    candidates = [p for p in staging.rglob("targets/m68k-amigaos") if p.is_dir()]
    if not candidates:
        fail(f"{archive.name}: no targets/m68k-amigaos/ anywhere in the archive")
    src = candidates[0]
    for need in ("include", "lib"):
        if not (src / need).is_dir():
            fail(f"{archive.name}: targets/m68k-amigaos/{need} is missing")

    if target.exists():
        shutil.rmtree(target)
    ensure_dirs(target.parent)
    shutil.move(str(src), str(target))
    shutil.rmtree(staging, ignore_errors=True)

    ok(f"{'m68k target':12s} installed to {rel(target)}")
    return True


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--force", action="store_true", help="reinstall even if present")
    args = ap.parse_args()

    ensure_dirs(TOOLCHAIN_SDK, TOOLCHAIN_TARGETS)

    step("Installing SDKs")
    good = True
    good &= install_vbcc_target(args.force)
    for sid, spec in SDKS.items():
        good &= install_sdk(sid, spec, args.force)

    missing_seeded = [d for d in SEEDED if not (TOOLCHAIN_SDK / d).exists()]
    if missing_seeded:
        warn("SDKs without an upstream download are not installed yet: "
             + ", ".join(missing_seeded))
        info("run: python3 bootstrap/acd_seed_from_legacy.py")

    return 0 if good else 1


if __name__ == "__main__":
    raise SystemExit(main())
