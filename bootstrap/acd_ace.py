#!/usr/bin/env python3
"""Unpack upstream ACE BASIC and apply the AmiDevIDE patch series.

    acd_ace.py [--force]

ACE BASIC is not part of this repository.  What is here is a series of
patches (compilers/acebasic/patches/) against one pinned upstream release;
the release itself is fetched by acd_fetch.py like every other source.  This
script turns the two into a buildable tree:

    var/downloads/ACEBasic-<tag>.tar.gz     upstream, checked against the lock
      -> var/src/acebasic/                  unpacked, committed as `upstream`
      -> git am compilers/acebasic/patches/*.patch

The result is a git repository, so the patches show up as ordinary commits:
`git log upstream..` lists them, and `git format-patch upstream..` writes them
back out after a change.  compilers/acebasic/UPDATING.md walks through moving
the series to a new upstream release.
"""

import argparse
import hashlib
import json
import shutil
import subprocess
from pathlib import Path

from acd_common import (
    ACEBASIC_PATCHES as PATCH_DIR, DOWNLOADS, SOURCES_LOCK, SRC,
    ensure_dirs, fail, have, info, ok, rel, run, step, warn,
)
from acd_extract import extract

SOURCE_ID = "acebasic"
DEST = SRC / "acebasic"
STAMP = DEST / ".git" / "acd-series"

# The patches carry their own authors; this identity only signs the
# `upstream` import commit and the committer field, neither of which leaves
# the machine.
GIT = ["git",
       "-c", "user.name=AmiDevIDE bootstrap",
       "-c", "user.email=bootstrap@localhost",
       "-c", "core.autocrlf=false",
       "-c", "init.defaultBranch=main",
       "-c", "commit.gpgsign=false"]


def _sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def _locked_archive() -> tuple[Path, str]:
    lock = json.loads(SOURCES_LOCK.read_text()) if SOURCES_LOCK.exists() else {}
    entry = lock.get(SOURCE_ID)
    if not entry or not entry.get("sha256"):
        fail(f"{SOURCE_ID} is not pinned in {rel(SOURCES_LOCK)}")
    archive = DOWNLOADS / entry["filename"]
    if not archive.is_file():
        fail(f"{archive.name} is missing from {rel(DOWNLOADS)}",
             "run: python3 bootstrap/acd_fetch.py")
    return archive, entry["sha256"]


def _series_id(archive_sha: str, patches: list[Path]) -> str:
    """Fingerprint of upstream + series, so a changed patch forces a rebuild."""
    h = hashlib.sha256(archive_sha.encode())
    for p in patches:
        h.update(p.name.encode())
        h.update(p.read_bytes())
    return h.hexdigest()


def _dirty(tree: Path) -> bool:
    proc = subprocess.run(["git", "status", "--porcelain", "--untracked-files=no"],
                          cwd=tree, capture_output=True, text=True)
    return proc.returncode != 0 or bool(proc.stdout.strip())


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--force", action="store_true",
                    help="discard var/src/acebasic, including local edits, and start over")
    args = ap.parse_args()

    step("Preparing ACE BASIC (upstream + patches)")
    if not have("git"):
        fail("git is required to apply the ACE patch series")

    archive, want_sha = _locked_archive()
    got_sha = _sha256(archive)
    if got_sha != want_sha:
        fail(f"{archive.name} does not match the pinned release",
             f"expected sha256 {want_sha}\n        got             {got_sha}\n"
             "        the patches only apply to that exact tree; delete the "
             "archive and re-run acd_fetch.py")

    patches = sorted(PATCH_DIR.glob("*.patch"))
    if not patches:
        fail(f"no patches in {rel(PATCH_DIR)}")
    series = _series_id(want_sha, patches)

    if DEST.exists() and not args.force:
        if STAMP.is_file() and STAMP.read_text().strip() == series:
            ok(f"{rel(DEST)} is up to date ({len(patches)} patches)")
            return 0
        if (DEST / ".git").is_dir() and _dirty(DEST):
            fail(f"{rel(DEST)} has uncommitted changes and the patch series changed",
                 "commit them and export with `git format-patch upstream..`, "
                 "or re-run with --force to discard them")
        info("patch series or upstream changed -- rebuilding the tree")

    if DEST.exists():
        shutil.rmtree(DEST)
    ensure_dirs(DEST.parent)

    info(f"unpacking {archive.name}")
    extract(archive, DEST, strip_prefix=True)

    run(GIT + ["init", "-q"], cwd=DEST, quiet=True)
    run(GIT + ["add", "-A"], cwd=DEST, quiet=True)
    run(GIT + ["commit", "-q", "-m", f"Upstream {archive.name}, unmodified"],
        cwd=DEST, quiet=True)
    run(GIT + ["tag", "upstream"], cwd=DEST, quiet=True)

    info(f"applying {len(patches)} patches from {rel(PATCH_DIR)}")
    proc = subprocess.run(GIT + ["am", "-q", "--keep-cr"] + [str(p) for p in patches],
                          cwd=DEST, capture_output=True, text=True)
    if proc.returncode != 0:
        print(proc.stdout + proc.stderr)
        fail("the ACE patch series does not apply",
             f"inspect with: git -C {rel(DEST)} am --show-current-patch=diff\n"
             "        see compilers/acebasic/UPDATING.md")

    STAMP.write_text(series + "\n")
    ok(f"{rel(DEST)}: upstream + {len(patches)} patches")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
