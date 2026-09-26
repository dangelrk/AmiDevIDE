#!/usr/bin/env python3
"""Check that the generated toolchain is complete and actually works.

Runs every tool it finds, so this catches a binary that was built for the
wrong architecture or is missing a shared library -- not just a missing file.

    acd_doctor.py [--quiet]

Exit status is 0 when everything essential is present, 1 otherwise.  Optional
pieces (the emulator, the seeded SDKs) are reported but do not fail the run.
"""

import argparse
import subprocess
import sys
from pathlib import Path

from acd_common import (
    EMU, EXEEXT, SHARED, TOOLCHAIN, TOOLCHAIN_BIN, TOOLCHAIN_CONFIG,
    TOOLCHAIN_SDK, TOOLCHAIN_TARGETS, host_platform, info, ok, rel, step, warn,
)

#: name -> (probe arguments, substring expected in the output)
#: The probe is what makes this more than an ls: a cross-built or half-linked
#: binary exists but cannot run.
TOOLS = {
    "vc":             (["-v"], "vbcc"),
    "vbccm68k":       ([], "vbcc"),
    "vasmm68k_mot":   (["-v"], "vasm"),
    # vlink's -V wants an argument; bare invocation prints the banner.
    "vlink":          ([], "vlink"),
    "vobjdump":       ([], "vobjdump"),
    "vprof":          ([], ""),
    "dtgen":          ([], "dtgen"),
    "ace":            (["-V"], "ACE"),
    "yap":            (["-V"], "yap"),
    "bas":            (["-V"], "bas"),
}

REQUIRED_SDKS = {
    "NDK_3.2": "Include_H",
    "MUI_3.8": "Developer/C/Include",
}

SEEDED_SDKS = {
    "MCC": "Developer/C/include",
    "regex": "include",
    "reqtools": "include",
    "Roadshow": "netinclude",
    "AmiSSL-v5": "Developer/include",
    "SQLite3": "include",
    "PosixLib": "include",
}

CONFIGS = ["aos68k", "aos68km", "aos68kr", "vc.config"]


class Report:
    def __init__(self) -> None:
        self.failures: list[str] = []
        self.warnings: list[str] = []

    def bad(self, msg: str) -> None:
        self.failures.append(msg)
        warn(msg)

    def soft(self, msg: str) -> None:
        self.warnings.append(msg)
        warn(msg)


def probe(binary: Path, args: list[str], expect: str) -> tuple[bool, str]:
    import os
    # vc reads its driver config through $VBCC; without it the probe fails with
    # "No config file!" and tells us nothing about the binary.
    env = os.environ.copy()
    env["VBCC"] = str(TOOLCHAIN)
    env["PATH"] = str(TOOLCHAIN_BIN) + os.pathsep + env.get("PATH", "")
    try:
        # These banners are Latin-1 (ACE's carries a copyright sign), so decode
        # leniently rather than let one byte abort the whole check.
        proc = subprocess.run([str(binary)] + args, capture_output=True,
                              text=True, encoding="utf-8", errors="replace",
                              timeout=20, input="", env=env)
    except OSError as exc:
        return False, f"cannot execute ({exc.strerror or exc})"
    except subprocess.TimeoutExpired:
        return False, "timed out (waiting for input?)"
    blob = (proc.stdout or "") + (proc.stderr or "")
    if expect and expect.lower() not in blob.lower():
        first = blob.strip().splitlines()[:1]
        return False, f"ran but did not identify as {expect!r}: {first}"
    version = ""
    for line in blob.splitlines():
        if line.strip():
            version = line.strip()
            break
    return True, version[:70]


def check_tools(r: Report) -> None:
    step("Toolchain binaries")
    for name, (args, expect) in TOOLS.items():
        binary = TOOLCHAIN_BIN / (name + EXEEXT)
        if not binary.is_file():
            r.bad(f"{name:14s} missing from {rel(TOOLCHAIN_BIN)}")
            continue
        good, detail = probe(binary, args, expect)
        if good:
            ok(f"{name:14s} {detail}")
        else:
            r.bad(f"{name:14s} {detail}")


def check_targets(r: Report) -> None:
    step("vbcc m68k-amigaos runtime")
    root = TOOLCHAIN_TARGETS / "m68k-amigaos"
    for sub, sample in (("include", "stdio.h"), ("lib", "vc.lib")):
        p = root / sub / sample
        if p.is_file():
            n = sum(1 for _ in (root / sub).rglob("*") if _.is_file())
            ok(f"{sub:14s} {n:,} files ({rel(root / sub)})")
        else:
            r.bad(f"{sub:14s} missing {rel(p)}")
    startup = root / "lib" / "startup.o"
    if not startup.is_file():
        r.bad("startup.o     missing -- linking will fail")


def check_sdks(r: Report) -> None:
    step("SDKs")
    for name, marker in REQUIRED_SDKS.items():
        p = TOOLCHAIN_SDK / name / marker
        if p.exists():
            ok(f"{name:14s} {rel(TOOLCHAIN_SDK / name)}")
        else:
            r.bad(f"{name:14s} missing {rel(p)} -- run acd_install_sdk.py")
    for name, marker in SEEDED_SDKS.items():
        p = TOOLCHAIN_SDK / name / marker
        if p.exists():
            ok(f"{name:14s} {rel(TOOLCHAIN_SDK / name)}")
        else:
            r.soft(f"{name:14s} not installed -- run acd_seed_from_legacy.py")

    for retired in ("NDK_3.9", "MUI_5.0"):
        if (TOOLCHAIN_SDK / retired).exists():
            r.soft(f"{retired:14s} still present but no longer used by anything")

    step("Autodocs and prototypes")
    autodocs = [
        TOOLCHAIN_SDK / "NDK_3.2" / "Autodocs",
        TOOLCHAIN_SDK / "MUI_3.8" / "Developer" / "Autodocs",
        TOOLCHAIN_SDK / "MCC" / "Developer" / "Autodocs" / "MCC",
    ]
    total = 0
    for d in autodocs:
        n = len(list(d.glob("*.doc"))) if d.is_dir() else 0
        total += n
        (ok if n else r.soft)(f"{n:4d} .doc in {rel(d)}")
    sfd = TOOLCHAIN_SDK / "NDK_3.2" / "SFD"
    n = len(list(sfd.glob("*.sfd"))) if sfd.is_dir() else 0
    if n:
        ok(f"{n:4d} .sfd in {rel(sfd)}")
    else:
        r.bad("no .sfd files -- call tips and signature help will be empty")


def check_configs(r: Report) -> None:
    step("Generated configuration")
    for name in CONFIGS:
        p = TOOLCHAIN_CONFIG / name
        if not p.is_file():
            r.bad(f"{name:14s} missing -- run acd_gen_config.py")
            continue
        text = p.read_text()
        # The whole point of generating these: vc execs its tools directly,
        # so an unexpanded ~ or $VAR in here silently breaks every build.
        if "~" in text or "$" in text:
            r.bad(f"{name:14s} contains an unexpanded ~ or $ -- regenerate it")
        elif str(TOOLCHAIN) not in text:
            r.bad(f"{name:14s} does not point at {rel(TOOLCHAIN)} -- regenerate it")
        else:
            ok(f"{name:14s} absolute paths, points here")

    ace = TOOLCHAIN_CONFIG / "ace68k"
    if ace.is_file():
        ok(f"{'ace68k':14s} {rel(ace)}")
    else:
        r.soft(f"{'ace68k':14s} missing -- run ACE's `make -f Makefile-host install`")

    for name in ("setenv.sh", "setenv.ps1"):
        p = TOOLCHAIN / name
        (ok if p.is_file() else r.bad)(
            f"{name:14s} {rel(p) if p.is_file() else 'missing'}")


def check_emulator(r: Report) -> None:
    step("Emulator")
    config = TOOLCHAIN / "fs-uae" / "development.fs-uae"
    if not config.is_file():
        r.soft("no FS-UAE machine description -- run acd_gen_config.py")
        return
    text = config.read_text()
    ok(f"{'machine':14s} {rel(config)}")
    for line in text.splitlines():
        if "MISSING" in line:
            r.soft(f"{'':14s} {line.split('=', 1)[0].strip()} is unset "
                   f"-- see emulator/images/README.md")
    for line in text.splitlines():
        if "=" not in line or "MISSING" in line:
            continue
        key, _, value = line.partition("=")
        key, value = key.strip(), value.strip()
        # Exact match: "hard_drive_0" also prefixes _label and _read_only,
        # whose values ("DH0", "0") are not paths at all.
        if key not in ("hard_drive_0", "kickstart_file") or not value:
            continue
        if not Path(value).exists():
            r.soft(f"{'':14s} {key} points at a missing file: {value}")

    if not SHARED.is_dir():
        r.soft(f"{'shared':14s} {rel(SHARED)} does not exist yet")
    else:
        ok(f"{'shared':14s} {rel(SHARED)}")

    sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "emulator"))
    try:
        from acd_emu import find_emulator
        emu = find_emulator()
        if emu:
            ok(f"{'fs-uae':14s} {emu}")
        else:
            r.soft(f"{'fs-uae':14s} not installed -- builds work, Run does not")
    except Exception as exc:  # never let the optional check fail the run
        r.soft(f"{'fs-uae':14s} could not probe ({exc})")


def check_compile(r: Report) -> None:
    """The end-to-end proof: compile and link a real Amiga executable."""
    step("End-to-end compile")
    vc = TOOLCHAIN_BIN / ("vc" + EXEEXT)
    if not vc.is_file():
        r.bad("vc is missing, skipping the compile check")
        return

    import tempfile
    with tempfile.TemporaryDirectory() as tmp:
        src = Path(tmp) / "acd_probe.c"
        out = Path(tmp) / "acd_probe"
        src.write_text('#include <stdio.h>\n'
                       'int main(void){printf("ok\\n");return 0;}\n')
        import os
        # vc resolves "+aos68k" to $VBCC/config/aos68k and execs vbccm68k,
        # vasmm68k_mot and vlink by bare name -- so a build needs both of
        # these set, which is exactly what toolchain/setenv.sh does.
        full = os.environ.copy()
        full["VBCC"] = str(TOOLCHAIN)
        full["PATH"] = str(TOOLCHAIN_BIN) + os.pathsep + full.get("PATH", "")
        proc = subprocess.run(
            [str(vc), "+aos68k", "-c99", "-cpu=68000", "-O2",
             str(src), "-o", str(out)],
            capture_output=True, text=True, timeout=120, env=full)
        if proc.returncode != 0:
            r.bad("vc could not build a hello-world:\n"
                  + (proc.stderr or proc.stdout).strip()[:600])
            return
        if not out.is_file():
            r.bad("vc reported success but produced no binary")
            return
        # Amiga hunk executables start with HUNK_HEADER, 0x000003F3.
        magic = out.read_bytes()[:4]
        if magic != b"\x00\x00\x03\xf3":
            r.bad(f"output is not an AmigaOS hunk executable (magic {magic!r})")
            return
        ok(f"vc built and linked a {out.stat().st_size:,}-byte AmigaOS executable")
        info("with no explicit -I/-L: the generated config supplies them")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--quiet", action="store_true", help="only report problems")
    ap.parse_args()

    if not TOOLCHAIN.is_dir():
        warn(f"{rel(TOOLCHAIN)} does not exist -- nothing has been bootstrapped yet")
        info("run: cmake --build build --target acd-toolchain")
        return 1

    r = Report()
    check_tools(r)
    check_targets(r)
    check_sdks(r)
    check_configs(r)
    check_emulator(r)
    check_compile(r)

    print()
    if r.failures:
        warn(f"{len(r.failures)} problem(s) found:")
        for f in r.failures:
            print(f"        {f}")
        return 1
    if r.warnings:
        ok(f"toolchain is usable ({len(r.warnings)} optional item(s) missing)")
    else:
        ok("toolchain is complete")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
