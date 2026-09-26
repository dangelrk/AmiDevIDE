#!/usr/bin/env python3
"""Copy a built binary into SHARED: and start FS-UAE on it.

    acd_emu.py --binary path/to/prog [--icon path/to/prog.info]
    acd_emu.py --project path/to/thing.aproj
    acd_emu.py --status          report what is installed, launch nothing
    acd_emu.py --serial          tail the emulator's serial log and exit on Ctrl-C

Same contract the IDE's Run command uses, so the two cannot drift: the binary
is copied into the host directory FS-UAE mounts as SHARED:, and the emulator is
started only if it is not already running -- SHARED: is a live mount, so a
running emulator picks the new binary up without a restart.
"""

import argparse
import json
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "bootstrap"))

from acd_common import (  # noqa: E402
    EMU, SHARED, TOOLCHAIN, ensure_dirs, host_platform, info, ok, rel, step, warn,
)

CONFIG = TOOLCHAIN / "fs-uae" / "development.fs-uae"
SERIAL_LOG = SHARED / "serial.log"

CANDIDATES = {
    "macos": [
        "/Applications/FS-UAE.app/Contents/MacOS/fs-uae",
        "/opt/homebrew/bin/fs-uae",
        "/usr/local/bin/fs-uae",
    ],
    "linux": ["/usr/bin/fs-uae", "/usr/local/bin/fs-uae", "/snap/bin/fs-uae"],
    "windows": [
        r"C:\Program Files\FS-UAE\Windows\x86-64\fs-uae.exe",
        r"C:\Program Files\FS-UAE\fs-uae.exe",
        r"C:\Program Files (x86)\FS-UAE\Windows\x86\fs-uae.exe",
    ],
}


def find_emulator() -> Path | None:
    for cand in CANDIDATES.get(host_platform(), []):
        p = Path(cand)
        if p.exists():
            return p
    found = shutil.which("fs-uae") or shutil.which("fs-uae.exe")
    return Path(found) if found else None


def is_running(exe_name: str) -> bool:
    """Mirrors aide_emulator_is_running() in core/, including on Windows."""
    try:
        if host_platform() == "windows":
            out = subprocess.run(
                ["tasklist", "/FI", f"IMAGENAME eq {exe_name}", "/NH"],
                capture_output=True, text=True, timeout=10).stdout
            return exe_name.lower() in out.lower()
        out = subprocess.run(["pgrep", "-x", exe_name],
                             capture_output=True, text=True, timeout=10)
        return out.returncode == 0
    except (OSError, subprocess.SubprocessError):
        # Cannot tell -- say no. Launching a second emulator is the milder
        # wrong answer than silently refusing to launch the first.
        return False


def binary_from_project(aproj: Path) -> tuple[Path, str, str]:
    """(built binary, name in SHARED:, program type) from an .aproj file."""
    data = json.loads(aproj.read_text())
    out = data.get("output", {})
    name = out.get("binary") or aproj.stem
    # The IDE builds into <project>/build/, see aide_makegen.c.
    binary = aproj.parent / "build" / name
    if not binary.exists():
        alt = aproj.parent / "bin" / name
        if alt.exists():
            binary = alt
    ptype = data.get("program", {}).get("type", "cli")
    return binary, name, ptype


def do_status() -> int:
    step("FS-UAE status")
    emu = find_emulator()
    if emu:
        ok(f"emulator:  {emu}")
        info(f"running:   {'yes' if is_running(emu.name) else 'no'}")
    else:
        warn("emulator:  not found -- install FS-UAE")

    if CONFIG.exists():
        ok(f"machine:   {rel(CONFIG)}")
        missing = [line.split("=", 1)[0].strip()
                   for line in CONFIG.read_text().splitlines()
                   if "MISSING" in line]
        for key in missing:
            warn(f"           {key} is not set -- see emulator/images/README.md")
    else:
        warn(f"machine:   {rel(CONFIG)} does not exist yet")
        info("           run: python3 bootstrap/acd_gen_config.py")

    ok(f"shared:    {rel(SHARED)}")
    for sub, label in (("kickstarts", "kickstart"), ("hdf", "hard disk"), ("adf", "floppy")):
        d = EMU / sub
        n = len(list(d.glob("*"))) if d.is_dir() else 0
        info(f"{label + ':':11s}{n} file(s) in {rel(d)}")
    return 0


def do_serial() -> int:
    step(f"Tailing {rel(SERIAL_LOG)}  (Ctrl-C to stop)")
    ensure_dirs(SHARED)
    SERIAL_LOG.touch(exist_ok=True)
    with SERIAL_LOG.open("r", errors="replace") as fh:
        fh.seek(0, os.SEEK_END)
        try:
            while True:
                line = fh.readline()
                if line:
                    sys.stdout.write(line)
                    sys.stdout.flush()
                else:
                    time.sleep(0.3)
        except KeyboardInterrupt:
            print()
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    src = ap.add_mutually_exclusive_group()
    src.add_argument("--binary", help="an already-built Amiga executable")
    src.add_argument("--project", help="an .aproj file to take the binary from")
    ap.add_argument("--icon", help="Workbench .info to copy alongside the binary")
    ap.add_argument("--subdir", default="", help="subdirectory inside SHARED:")
    ap.add_argument("--config", help="override the FS-UAE configuration file")
    ap.add_argument("--status", action="store_true", help="report state and exit")
    ap.add_argument("--serial", action="store_true", help="tail the serial log")
    ap.add_argument("--no-launch", action="store_true",
                    help="copy into SHARED: but do not start the emulator")
    args, passthrough = ap.parse_known_args()

    if args.status:
        return do_status()
    if args.serial:
        return do_serial()

    if not args.binary and not args.project:
        ap.error("one of --binary, --project, --status or --serial is required")

    icon = Path(args.icon) if args.icon else None
    if args.project:
        aproj = Path(args.project)
        if not aproj.is_file():
            warn(f"no such project: {aproj}")
            return 1
        binary, name, ptype = binary_from_project(aproj)
        if icon is None and ptype == "wb":
            candidate = binary.with_name(binary.name + ".info")
            icon = candidate if candidate.exists() else None
    else:
        binary = Path(args.binary)
        name = binary.name

    if not binary.is_file():
        warn(f"binary not found: {binary}")
        info("build the project first")
        return 1

    dest_dir = SHARED / args.subdir if args.subdir else SHARED
    ensure_dirs(dest_dir)

    step(f"Installing {name} into SHARED:")
    dest = dest_dir / name
    shutil.copy2(binary, dest)
    ok(f"{rel(dest)}  ({dest.stat().st_size:,} bytes)")
    if icon and icon.is_file():
        shutil.copy2(icon, dest.with_name(dest.name + ".info"))
        ok(f"{rel(dest)}.info")

    if args.no_launch:
        return 0

    emu = find_emulator()
    if not emu:
        warn("FS-UAE not found -- copied into SHARED: but nothing was launched")
        return 1

    if is_running(emu.name):
        ok("FS-UAE is already running; SHARED: is a live mount, so start the "
           "program from the Amiga shell.")
        return 0

    config = Path(args.config) if args.config else CONFIG
    if not config.is_file():
        warn(f"no FS-UAE configuration at {config}")
        info("run: python3 bootstrap/acd_gen_config.py")
        return 1

    step(f"Launching {emu.name}")
    info(f"config: {rel(config)}")
    subprocess.Popen([str(emu), str(config)] + passthrough,
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                     start_new_session=(host_platform() != "windows"))
    ok("started")
    info(f"program output goes to {rel(SERIAL_LOG)}  (acd_emu.py --serial)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
