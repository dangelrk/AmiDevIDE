#!/usr/bin/env python3
"""Build vbcc, vasm and vlink from source, then install them into toolchain/bin.

    acd_build_tools.py [--force] [--only vbcc,vasm,vlink]

Everything is unpacked into var/src/<tool>/ and built there; nothing is written
back into the downloaded archives, so --force can always start over.

The upstream Makefiles hardcode `gcc` with flags no current clang accepts
(vbcc asks for -std=c9x), which is why CC is passed in on the command line
rather than the sources being patched.
"""

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

from acd_common import (
    BUILD, DOWNLOADS, EXEEXT, SRC, TOOLCHAIN_BIN,
    cc_program, ensure_dirs, fail, host_platform, info, make_program, ok, rel,
    run, step, warn,
)
from acd_extract import extract


def _unpack(archive_glob: str, dest: Path, force: bool) -> Path:
    """Unpack the newest matching archive into dest and return the source root."""
    if dest.exists() and not force:
        return dest
    hits = sorted(DOWNLOADS.glob(archive_glob))
    if not hits:
        fail(f"no archive matching {archive_glob} in {rel(DOWNLOADS)}",
             "run: python3 bootstrap/acd_fetch.py")
    if dest.exists():
        shutil.rmtree(dest)
    extract(hits[-1], dest, strip_prefix=True)
    return dest


def _install(binary: Path, name: str | None = None) -> Path:
    ensure_dirs(TOOLCHAIN_BIN)
    target = TOOLCHAIN_BIN / (name or binary.name)
    shutil.copy2(binary, target)
    target.chmod(0o755)
    return target


# ------------------------------------------------------------------- vbcc

def _run_dtgen(src: Path, target: str, cc_line: str, make: str) -> None:
    """Generate objects/<target>/dt.{h,c}, the target's data-type description.

    dtgen is an interactive questionnaire, which is why upstream ships no
    scripted way to build vbcc.  Every question has a default that its own
    probing worked out, and for a 64-bit little-endian host targeting m68k
    those defaults are the correct answers:

        cross-compiler?            y   (host != target, so probe the host types)
        8/16/32-bit integer types  y   mapped onto char/short/int
        big-endian 16/32/64-bit    n   host is little-endian, vbcc converts
        big-endian IEEE float      n   likewise

    The result is what m68k needs: 32-bit long, 32-bit pointer, 64-bit long
    long, and floats held as byte arrays with software conversion.  Feeding
    bare newlines accepts each default in turn.
    """
    objdir = src / "objects" / target
    ensure_dirs(objdir, src / "bin")

    dtgen = src / "bin" / f"dtgen{EXEEXT}"
    run([make, f"bin/dtgen{EXEEXT}", f"NCC={cc_line}", "NLDFLAGS=-lm"],
        cwd=src, quiet=True)
    if not dtgen.is_file():
        fail("vbcc: dtgen did not build")

    machine_dt = src / "machines" / target / "machine.dt"
    if not machine_dt.is_file():
        fail(f"vbcc: no machine.dt for target {target}")

    info(f"generating objects/{target}/dt.h via dtgen (defaults accepted)")
    proc = subprocess.run(
        [str(dtgen), str(machine_dt), str(objdir / "dt.h"), str(objdir / "dt.c")],
        cwd=src, input="\n" * 400, capture_output=True, text=True, timeout=120,
    )
    if proc.returncode != 0:
        sys.stderr.write(proc.stdout)
        fail(f"vbcc: dtgen failed ({proc.returncode})")

    header = objdir / "dt.h"
    if not header.is_file() or header.stat().st_size == 0:
        fail("vbcc: dtgen produced an empty dt.h")
    # A wrong answer would silently mistype the whole compiler, so assert the
    # two widths that matter most for m68k rather than trust the run.
    text = header.read_text()
    for want in ("typedef signed int zlong;", "typedef unsigned int zpointer;"):
        if want not in text:
            fail(f"vbcc: dt.h does not describe an m68k target (missing: {want})",
                 "expected 32-bit long and 32-bit pointer")
    ok(f"dt.h describes m68k: 32-bit long, 32-bit pointer")


def build_vbcc(force: bool) -> None:
    src = _unpack("vbcc*.tar.gz", SRC / "vbcc", force)
    make = make_program()
    cc = cc_program()

    # The shipped Makefile uses `gcc -std=c9x`, which clang rejects outright and
    # newer gcc only tolerates.  -std=c99 is what c9x became.  HAVE_AOS4 is what
    # upstream builds with; it enables the AmigaOS4 target hooks the m68k
    # backend also expects to be present.
    cflags = f"{cc} -std=c99 -O2 -DHAVE_AOS4"

    ensure_dirs(src / "bin", src / "objects")
    _run_dtgen(src, "m68k", cflags, make)

    info("building vbcc for TARGET=m68k")
    run([make, "TARGET=m68k", f"CC={cflags}", f"NCC={cflags}", "LDFLAGS=-lm"], cwd=src)

    produced = {
        "vc": src / "bin" / f"vc{EXEEXT}",
        "vbccm68k": src / "bin" / f"vbccm68k{EXEEXT}",
        "vprof": src / "bin" / f"vprof{EXEEXT}",
        # dtgen is a build tool, not a user tool, but the native AmigaOS build
        # of vbcc needs it later (Makefile.aos runs it under vamos), so keep it.
        "dtgen": src / "bin" / f"dtgen{EXEEXT}",
    }
    for name, path in produced.items():
        if not path.is_file():
            fail(f"vbcc build finished but {name} is missing at {path}")
        ok(f"installed {rel(_install(path))}")


# ------------------------------------------------------------------- vasm

def build_vasm(force: bool) -> None:
    src = _unpack("vasm*.tar.gz", SRC / "vasm", force)
    make = make_program()
    cc = cc_program()

    makefile = "Makefile"
    if host_platform() == "windows":
        makefile = "Makefile.Cygwin"   # MSYS2/MinGW gcc

    ensure_dirs(src / "objects")
    info("building vasm for CPU=m68k SYNTAX=mot")
    run([make, "-f", makefile, "CPU=m68k", "SYNTAX=mot", f"CC={cc}"], cwd=src)

    binary = src / f"vasmm68k_mot{EXEEXT}"
    if not binary.is_file():
        fail(f"vasm build finished but {binary.name} is missing")
    ok(f"installed {rel(_install(binary))}")

    # vobjdump ships alongside and the IDE's memory map uses it.
    objdump = src / f"vobjdump{EXEEXT}"
    if not objdump.is_file():
        run([make, "-f", makefile, f"CC={cc}", f"vobjdump{EXEEXT}"], cwd=src, check=False)
    if objdump.is_file():
        ok(f"installed {rel(_install(objdump))}")
    else:
        warn("vobjdump was not built -- the IDE's memory map will be unavailable")


# ------------------------------------------------------------------- vlink

def build_vlink(force: bool) -> None:
    src = _unpack("vlink*.tar.gz", SRC / "vlink", force)
    make = make_program()
    cc = cc_program()

    makefile = "Makefile"
    if host_platform() == "windows" and (src / "Makefile.Cygwin").exists():
        makefile = "Makefile.Cygwin"

    # The Makefile writes into objects/ but does not create it.
    ensure_dirs(src / "objects")
    info("building vlink")
    run([make, "-f", makefile, f"CC={cc}"], cwd=src)

    binary = src / f"vlink{EXEEXT}"
    if not binary.is_file():
        fail(f"vlink build finished but {binary.name} is missing")
    ok(f"installed {rel(_install(binary))}")


BUILDERS = {
    "vbcc": build_vbcc,
    "vasm": build_vasm,
    "vlink": build_vlink,
}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--force", action="store_true",
                    help="re-unpack the sources and rebuild from scratch")
    ap.add_argument("--only", default="",
                    help="comma-separated subset of: " + ", ".join(BUILDERS))
    args = ap.parse_args()

    wanted = [t.strip() for t in args.only.split(",") if t.strip()] or list(BUILDERS)
    for tool in wanted:
        if tool not in BUILDERS:
            fail(f"unknown tool {tool!r}", "known: " + ", ".join(BUILDERS))

    ensure_dirs(SRC, BUILD, TOOLCHAIN_BIN)

    for tool in wanted:
        step(f"Building {tool} from source")
        BUILDERS[tool](args.force)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
