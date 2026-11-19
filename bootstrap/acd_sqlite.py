#!/usr/bin/env python3
"""Build SQLite 3 for vbcc from the Amiga port and install it as an SDK.

    acd_sqlite.py [--force]

The Aminet archive (Steffen Gutmann's 3.34.0 port, source id `sqlite3`)
carries a libsqlite3.a, but that is built with gcc 3.4.0 for libnix and is no
use to vbcc.  So this builds the library from the same archive:

    var/downloads/sqlite-3.34.0.a-amiga.lha   checked against the lock
      -> var/build/sqlite/                     unpacked
      -> make sqlite3.c                        amalgamation; host cc + tclsh
      -> os_amiga.c patched for vbcc           (see _patch_os_amiga)
      -> vc: sqlite3.o + os_amiga.o            68000, large data
      -> toolchain/sdk/SQLite3/{include,lib/sqlite3.lib}

sqlite3.lib is the two hunk objects joined, which is all an Amiga link
library is.  Programs link it with `-lsqlite3 -lmieee -lamiga` (IEEE floating
point; amiga.lib supplies NewList).  Tested on the Amiga: RAM: and FFS,
64-bit integers, floating point, timestamps, two processes writing one
database at the same time.

SQLite3 is optional.  Without tclsh on the host this only warns, so the rest
of the toolchain still comes up.
"""

import argparse
import hashlib
import json
import os
import re
import shutil
from pathlib import Path

from acd_common import (
    BUILD, DOWNLOADS, SOURCES_LOCK, TOOLCHAIN, TOOLCHAIN_BIN, TOOLCHAIN_SDK,
    EXEEXT, cc_program, ensure_dirs, fail, have, host_platform, info,
    make_program, ok, rel, rmtree, run, step, warn,
)
from acd_extract import extract

SOURCE_ID = "sqlite3"
WORK = BUILD / "sqlite"
TARGET = TOOLCHAIN_SDK / "SQLite3"
STAMP = TARGET / ".acd-build"
#: Outside WORK, which build() starts by wiping.
SHIM = BUILD / "tclsh-shim"

#: The configuration both the amalgamation and the vbcc compile see.  No
#: threads, no WAL (needs shared memory), no loadable extensions; pointer
#: size and byte order spelled out because vbcc predefines neither in a form
#: SQLite recognises.
SQLITE_DEFS = [
    "-DNDEBUG=1",
    "-DSQLITE_OMIT_LOAD_EXTENSION=1",
    "-DSQLITE_OMIT_WAL=1",
    "-DSQLITE_OS_OTHER=1",
    "-DSQLITE_DISABLE_LFS",
    "-DSQLITE_PTRSIZE=4",
    "-DSQLITE_BYTEORDER=4321",
]

#: Only for the vbcc compile.  HAVE_STDINT_H matters: without it SQLite's
#: SQLITE_INT_TO_PTR becomes &((char*)0)[X], which vbcc rejects in static
#: initialisers ("initializer not a constant").
VBCC_DEFS = [
    "-DSQLITE_THREADSAFE=0",
    "-DHAVE_USLEEP=0",
    "-DHAVE_STDINT_H=1",
]

#: -O2, never -O1: at -O1 vbcc's m68k backend dies in
#: indexMightHelpWithOrderBy with "error 324: illegal use of register".
#: 68000 code with IEEE library calls, so the one library serves every CPU.
VC_FLAGS = ["+aos68k", "-c99", "-O2", "-cpu=68000"]

SMOKE_TEST = r"""
#include <stdio.h>
#include "sqlite3.h"
int main(void)
{
    sqlite3 *db;
    if (sqlite3_open(":memory:", &db) != SQLITE_OK) return 20;
    sqlite3_exec(db, "CREATE TABLE t(x)", 0, 0, 0);
    sqlite3_close(db);
    printf("%s\n", sqlite3_libversion());
    return 0;
}
"""


def _sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def _locked_archive() -> tuple[Path, str] | None:
    lock = json.loads(SOURCES_LOCK.read_text()) if SOURCES_LOCK.exists() else {}
    entry = lock.get(SOURCE_ID)
    if not entry or not entry.get("sha256"):
        fail(f"{SOURCE_ID} is not pinned in {rel(SOURCES_LOCK)}")
    archive = DOWNLOADS / entry["filename"]
    if not archive.is_file():
        return None
    return archive, entry["sha256"]


def _build_id(archive_sha: str) -> str:
    """Archive + this script: a changed flag or patch forces a rebuild."""
    h = hashlib.sha256(archive_sha.encode())
    h.update(Path(__file__).read_bytes())
    return h.hexdigest()


def _find_tclsh(shim_dir: Path) -> bool:
    """main.mk calls a bare `tclsh`.  Distributions often only have
    tclsh8.6 or tclsh9.0; a one-line shim on PATH covers that."""
    if have("tclsh"):
        return True
    for cand in ("tclsh9.0", "tclsh8.6", "tclsh8.5"):
        path = shutil.which(cand)
        if path and host_platform() != "windows":
            ensure_dirs(shim_dir)
            shim = shim_dir / "tclsh"
            shim.write_text(f'#!/bin/sh\nexec "{path}" "$@"\n')
            shim.chmod(0o755)
            return True
    return False


def _patch_os_amiga(path: Path) -> None:
    """Make os_amiga.c compile with vbcc.

    1. sqliteInt.h and <exec/types.h> both define GLOBAL.  gcc warns, vbcc
       stops with error 272, which cannot be turned off.
    2. vbcc has no __aligned.  AmigaIsDir() and AmigaAccess() need a
       longword-aligned FileInfoBlock for Examine(); FIB_DECL takes one from
       a 3-byte-larger buffer and aligns the pointer by hand, which works
       with any compiler.  `fib` becomes a pointer in both functions.
    """
    s = path.read_text(encoding="latin-1")

    old = "#include <dos/dos.h>\n"
    if s.count(old) != 1:
        fail(f"{path.name}: expected exactly one '#include <dos/dos.h>'")
    s = s.replace(old, "#undef GLOBAL\t\t\t/* sqliteInt.h and <exec/types.h> "
                       "both define it */\n" + old)

    anchor = "#  define ALIGNED __aligned\n#endif\n"
    if s.count(anchor) != 1:
        fail(f"{path.name}: the ALIGNED definition has moved")
    s = s.replace(anchor, anchor + "\n"
        "/* A longword-aligned FileInfoBlock without compiler extensions (vbcc has no\n"
        "** __aligned): FIB_DECL(fib) declares 'struct FileInfoBlock *fib'. */\n"
        "#define FIB_DECL(name) \\\n"
        "    UBYTE name##_buf[sizeof(struct FileInfoBlock) + 3]; \\\n"
        "    struct FileInfoBlock *name = \\\n"
        "        (struct FileInfoBlock *)(((ULONG)name##_buf + 3) & ~3UL)\n")

    for header in ("static int AmigaIsDir(", "static int AmigaAccess("):
        start = s.index(header)
        end = s.index("\n}\n", start)
        body = s[start:end]
        if body.count("ALIGNED struct FileInfoBlock fib;") != 1:
            fail(f"{path.name}: {header.strip('(')} no longer declares one aligned fib")
        body = body.replace("ALIGNED struct FileInfoBlock fib;", "FIB_DECL(fib);")
        body = body.replace("&fib", "fib")
        body = re.sub(r"\bfib\.", "fib->", body)
        s = s[:start] + body + s[end:]

    if "ALIGNED struct FileInfoBlock" in s:
        fail(f"{path.name}: an aligned FileInfoBlock outside the two known functions")
    path.write_text(s, encoding="latin-1", newline="")


def _amalgamation(top: Path, bld: Path, shim_dir: Path) -> None:
    ensure_dirs(bld)
    # main.mk is SQLite's own portable makefile; BCC is the host compiler
    # that builds lemon and mkkeywordhash.
    (bld / "Makefile").write_text(
        "TOP = ..\n"
        f"BCC = {cc_program()} -O2\n"
        f"OPTS += {' '.join(SQLITE_DEFS)}\n"
        "include $(TOP)/main.mk\n")
    env = {"PATH": str(shim_dir) + os.pathsep + os.environ.get("PATH", "")}
    proc = run([make_program(), "sqlite3.c"], cwd=bld, env=env, quiet=True,
               check=False)
    if proc.returncode != 0:
        tail = "\n".join(((proc.stdout or "") + (proc.stderr or "")).splitlines()[-15:])
        fail("generating the SQLite amalgamation failed", tail)
    for need in ("sqlite3.c", "sqlite3.h", "tsrc/os_amiga.c"):
        if not (bld / need).is_file():
            fail(f"the amalgamation step did not produce {need}")


def _vc(args: list, cwd: Path, what: str) -> None:
    env = {
        "VBCC": str(TOOLCHAIN),
        "PATH": str(TOOLCHAIN_BIN) + os.pathsep + os.environ.get("PATH", ""),
    }
    proc = run([TOOLCHAIN_BIN / f"vc{EXEEXT}", *args], cwd=cwd, env=env,
               quiet=True, check=False)
    out = (proc.stdout or "") + (proc.stderr or "")
    if proc.returncode != 0:
        tail = "\n".join(out.splitlines()[-15:])
        fail(f"vc failed: {what}", tail)
    warnings = out.count("warning ")
    info(f"{'':12s} {what}" + (f" ({warnings} warnings)" if warnings else ""))


def _is_hunk_executable(path: Path) -> bool:
    with path.open("rb") as fh:
        return fh.read(4) == b"\x00\x00\x03\xf3"


def build(archive: Path) -> None:
    if WORK.exists():
        rmtree(WORK)
    ensure_dirs(WORK)
    info(f"{'SQLite3':12s} unpacking {archive.name}")
    extract(archive, WORK / "src")
    tops = [p.parent for p in (WORK / "src").rglob("main.mk")]
    if len(tops) != 1:
        fail(f"{archive.name}: expected one source tree with main.mk")
    top = tops[0]
    bld = top / "bld"

    info(f"{'':12s} generating the amalgamation (host cc + tclsh)")
    _amalgamation(top, bld, SHIM)
    _patch_os_amiga(bld / "tsrc" / "os_amiga.c")

    ndk = TOOLCHAIN_SDK / "NDK_3.2" / "Include_H"
    defs = SQLITE_DEFS + VBCC_DEFS
    _vc([*VC_FLAGS, *defs, "-I.", "-c", "sqlite3.c", "-o", "sqlite3.o"],
        bld, "compiled sqlite3.c")
    _vc([*VC_FLAGS, *defs, "-I.", "-Itsrc", f"-I{ndk}", "-c",
         "tsrc/os_amiga.c", "-o", "os_amiga.o"], bld, "compiled os_amiga.c")

    stage = WORK / "stage"
    ensure_dirs(stage / "include", stage / "lib")
    shutil.copy2(bld / "sqlite3.h", stage / "include" / "sqlite3.h")
    with (stage / "lib" / "sqlite3.lib").open("wb") as lib:
        for obj in ("sqlite3.o", "os_amiga.o"):
            lib.write((bld / obj).read_bytes())

    # Link something against the staged SDK exactly as a project would.
    smoke = WORK / "smoke"
    ensure_dirs(smoke)
    (smoke / "smoke.c").write_text(SMOKE_TEST)
    _vc([*VC_FLAGS, f"-I{stage / 'include'}", "smoke.c", "-o", "smoke",
         f"-L{stage / 'lib'}", "-lsqlite3", "-lmieee", "-lamiga"],
        smoke, "linked a test program with -lsqlite3 -lmieee -lamiga")
    if not _is_hunk_executable(smoke / "smoke"):
        fail("the SQLite test program is not an AmigaOS executable")

    if TARGET.exists():
        rmtree(TARGET)
    ensure_dirs(TARGET.parent)
    shutil.move(str(stage), str(TARGET))
    rmtree(WORK)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--force", action="store_true", help="rebuild even if up to date")
    args = ap.parse_args()

    step("Building SQLite 3 for vbcc")
    found = _locked_archive()
    if not found:
        warn(f"{'SQLite3':12s} no {SOURCE_ID} archive in {rel(DOWNLOADS)} "
             "-- run acd_fetch.py; skipped")
        return 0
    archive, want_sha = found
    got_sha = _sha256(archive)
    if got_sha != want_sha:
        fail(f"{archive.name} does not match the pinned release",
             f"expected sha256 {want_sha}\n        got             {got_sha}")

    build_id = _build_id(want_sha)
    if (not args.force and STAMP.is_file()
            and STAMP.read_text().strip() == build_id
            and (TARGET / "lib" / "sqlite3.lib").is_file()):
        ok(f"{'SQLite3':12s} up to date: {rel(TARGET)}")
        return 0

    if not (TOOLCHAIN_BIN / f"vc{EXEEXT}").is_file():
        fail("vc is not built yet", "run acd_build_tools.py first")
    if not (TOOLCHAIN_SDK / "NDK_3.2" / "Include_H").is_dir():
        fail("NDK 3.2 is not installed yet", "run acd_install_sdk.py first")
    if not _find_tclsh(SHIM):
        warn(f"{'SQLite3':12s} no tclsh found -- SQLite3 is not built "
             "(optional; macOS has /usr/bin/tclsh, Linux: package tcl)")
        return 0

    if TARGET.exists() and not STAMP.is_file():
        info(f"{'SQLite3':12s} replacing the seeded SDK (its libsqlite3.a is "
             "gcc-built and unusable with vbcc)")
    build(archive)
    STAMP.write_text(build_id + "\n")
    ok(f"{'SQLite3':12s} installed to {rel(TARGET)} (lib/sqlite3.lib, include/sqlite3.h)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
