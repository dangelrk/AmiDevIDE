#!/usr/bin/env python3
"""Instantiate the templates into the generated toolchain root.

Writes, all with absolute paths baked in:

    toolchain/config/{aos68k,aos68km,aos68kr}   vc driver configs
    toolchain/setenv.sh, setenv.ps1             environment for a shell
    toolchain/fs-uae/development.fs-uae         emulator machine
    toolchain/project-template/Makefile*        ready-to-copy project skeleton

vc execs its tools directly rather than through a shell, so a "~" inside a
driver config never expands.  Generating these instead of checking them in is
what makes the old "edit the paths by hand / unpack config/mac.7z" step go away.

    acd_gen_config.py [--kickstart PATH] [--workbench PATH]
"""

import argparse
import os
import shutil
from pathlib import Path

from acd_common import (
    EMU, SHARED, TEMPLATES, TOOLCHAIN, TOOLCHAIN_CONFIG, VAR,
    ensure_dirs, host_platform, info, ok, rel, step, warn,
)

KICKSTART_NAMES = ["amiga-os-310-a1200.rom", "amiga-os-310.rom", "kick31.rom"]
WORKBENCH_NAMES = ["workbench-311.hdf", "workbench-310.hdf", "workbench.hdf"]


def _render(template: Path, target: Path, mapping: dict[str, str]) -> None:
    text = template.read_text()
    for key, value in mapping.items():
        text = text.replace(key, value)
    ensure_dirs(target.parent)
    target.write_text(text)


def _find_asset(explicit: str | None, names: list[str], subdir: str) -> Path | None:
    """Locate an emulator asset: explicit path, then var/emu/, then ~/FS-UAE/."""
    if explicit:
        p = Path(explicit).expanduser()
        return p if p.exists() else None
    roots = [EMU / subdir, Path.home() / "FS-UAE" / subdir.capitalize()]
    if subdir == "kickstarts":
        roots.append(Path.home() / "FS-UAE" / "Kickstarts")
    elif subdir == "hdf":
        roots.append(Path.home() / "FS-UAE" / "Hard Drives")
    for root in roots:
        for name in names:
            cand = root / name
            if cand.exists():
                return cand
    return None


def gen_vc_configs() -> None:
    src_dir = TEMPLATES / "vc-config"
    ensure_dirs(TOOLCHAIN_CONFIG)
    for tpl in sorted(src_dir.glob("*.in")):
        target = TOOLCHAIN_CONFIG / tpl.stem
        _render(tpl, target, {"@ACD_TOOLCHAIN@": str(TOOLCHAIN)})
        ok(f"{rel(target)}")
    # Historically config/vc.config was a byte-for-byte copy of aos68k that vc
    # picks up when no +profile is given.  Keep that convenience, generated.
    shutil.copy2(TOOLCHAIN_CONFIG / "aos68k", TOOLCHAIN_CONFIG / "vc.config")
    ok(f"{rel(TOOLCHAIN_CONFIG / 'vc.config')} (copy of aos68k)")


def gen_setenv() -> None:
    _render(TEMPLATES / "setenv" / "setenv.sh.in", TOOLCHAIN / "setenv.sh",
            {"@ACD_TOOLCHAIN@": str(TOOLCHAIN)})
    (TOOLCHAIN / "setenv.sh").chmod(0o644)
    ok(f"{rel(TOOLCHAIN / 'setenv.sh')}")

    win_path = str(TOOLCHAIN).replace("/", "\\")
    _render(TEMPLATES / "setenv" / "setenv.ps1.in", TOOLCHAIN / "setenv.ps1",
            {"@ACD_TOOLCHAIN_WIN@": win_path})
    ok(f"{rel(TOOLCHAIN / 'setenv.ps1')}")


def gen_fsuae(kickstart: str | None, workbench: str | None) -> None:
    ensure_dirs(SHARED, EMU / "kickstarts", EMU / "hdf", EMU / "adf")

    ks = _find_asset(kickstart, KICKSTART_NAMES, "kickstarts")
    wb = _find_asset(workbench, WORKBENCH_NAMES, "hdf")

    def as_path(p: Path | None, placeholder: str) -> str:
        if p is None:
            return placeholder
        s = str(p)
        return s.replace("/", "\\") if host_platform() == "windows" else s

    serial_log = SHARED / "serial.log"

    _render(TEMPLATES / "fs-uae" / "development.fs-uae.in",
            TOOLCHAIN / "fs-uae" / "development.fs-uae",
            {
                "@ACD_KICKSTART@": as_path(ks, "# MISSING -- see emulator/images/README.md"),
                "@ACD_WORKBENCH_HDF@": as_path(wb, "# MISSING -- see emulator/images/README.md"),
                "@ACD_SHARED@": as_path(SHARED, ""),
                "@ACD_SERIAL_LOG@": as_path(serial_log, ""),
            })
    ok(f"{rel(TOOLCHAIN / 'fs-uae' / 'development.fs-uae')}")
    if ks:
        info(f"kickstart: {ks}")
    else:
        warn("no Kickstart ROM found -- the emulator config is incomplete")
    if wb:
        info(f"workbench: {wb}")
    else:
        warn("no Workbench HDF found -- the emulator config is incomplete")


def gen_project_template() -> None:
    """A copy of the project Makefiles with this toolchain's path filled in."""
    target = TOOLCHAIN / "project-template"
    ensure_dirs(target)
    for f in sorted((TEMPLATES / "project-makefiles").iterdir()):
        if f.is_file():
            shutil.copy2(f, target / f.name)
    ok(f"{rel(target)} ({len(list(target.iterdir()))} files)")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--kickstart", help="path to the Kickstart 3.1 ROM")
    ap.add_argument("--workbench", help="path to the Workbench 3.1 HDF image")
    args = ap.parse_args()

    ensure_dirs(TOOLCHAIN)

    step("Generating vc driver configs")
    gen_vc_configs()

    step("Generating shell environment")
    gen_setenv()

    step("Generating the FS-UAE machine")
    gen_fsuae(args.kickstart, args.workbench)

    step("Generating the project template")
    gen_project_template()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
