"""Shared plumbing for the AmiDevIDE bootstrap scripts.

Every acd_* script imports this for paths, logging and subprocess handling so
that they agree on where things live and look the same on the terminal.
"""

import os
import shutil
import subprocess
import sys
from pathlib import Path

# ---------------------------------------------------------------- layout

ROOT = Path(__file__).resolve().parent.parent

BOOTSTRAP = ROOT / "bootstrap"
TEMPLATES = ROOT / "templates"
DOCS = ROOT / "docs"
IDE = ROOT / "ide"
ACEBASIC_PATCHES = ROOT / "compilers" / "acebasic" / "patches"
EMULATOR = ROOT / "emulator"

#: The generated toolchain root -- this is what $VBCC points at.  Its layout is
#: deliberately identical to the old ~/Dev/amiga tree so that every consumer
#: (ACE's Makefile-host, the IDE's aide_toolchain.c, the generated project
#: Makefiles) keeps working with nothing but a different prefix.
TOOLCHAIN = Path(os.environ.get("ACD_TOOLCHAIN", ROOT / "toolchain"))

VAR = Path(os.environ.get("ACD_VAR", ROOT / "var"))
DOWNLOADS = VAR / "downloads"
SRC = VAR / "src"
BUILD = VAR / "build"
EMU = VAR / "emu"
#: SHARED: as the emulator sees it.  Lives inside the generated toolchain
#: root, not under var/, so <toolchainRoot>/shared keeps meaning what it
#: always did -- the IDE derives its default from exactly that.
SHARED = TOOLCHAIN / "shared"

TOOLCHAIN_BIN = TOOLCHAIN / "bin"
TOOLCHAIN_SDK = TOOLCHAIN / "sdk"
TOOLCHAIN_CONFIG = TOOLCHAIN / "config"
TOOLCHAIN_TARGETS = TOOLCHAIN / "targets"

SOURCES_TOML = BOOTSTRAP / "sources.toml"
SOURCES_LOCK = BOOTSTRAP / "sources.lock.json"

# ---------------------------------------------------------------- platform

def host_platform() -> str:
    """macos / linux / windows -- the same three names Makefile.platform uses."""
    if os.name == "nt":
        return "windows"
    s = sys.platform
    if s == "darwin":
        return "macos"
    if s.startswith("linux"):
        return "linux"
    # MSYS2/MinGW/Cygwin python reports these
    if s.startswith(("msys", "cygwin", "win")):
        return "windows"
    return "linux"


EXEEXT = ".exe" if host_platform() == "windows" else ""

# ---------------------------------------------------------------- output

_COLOR = sys.stdout.isatty() and os.environ.get("NO_COLOR") is None


def _c(code: str, text: str) -> str:
    return f"\033[{code}m{text}\033[0m" if _COLOR else text


def step(msg: str) -> None:
    print(_c("1;36", f"==> {msg}"), flush=True)


def info(msg: str) -> None:
    print(f"    {msg}", flush=True)


def ok(msg: str) -> None:
    print(_c("32", f"    OK  {msg}"), flush=True)


def warn(msg: str) -> None:
    print(_c("33", f"    !   {msg}"), flush=True)


def fail(msg: str, hint: str | None = None) -> "NoReturn":  # type: ignore[valid-type]
    print(_c("1;31", f"    ERR {msg}"), file=sys.stderr, flush=True)
    if hint:
        print(f"        {hint}", file=sys.stderr, flush=True)
    raise SystemExit(1)


# ---------------------------------------------------------------- processes

def run(cmd, cwd=None, env=None, quiet=False, check=True):
    """Run a command, streaming or capturing depending on `quiet`."""
    printable = " ".join(str(c) for c in cmd)
    if not quiet:
        info(f"$ {printable}")
    full_env = None
    if env:
        full_env = os.environ.copy()
        full_env.update({k: str(v) for k, v in env.items()})
    proc = subprocess.run(
        [str(c) for c in cmd],
        cwd=str(cwd) if cwd else None,
        env=full_env,
        capture_output=quiet,
        text=True,
    )
    if check and proc.returncode != 0:
        if quiet:
            sys.stderr.write(proc.stdout or "")
            sys.stderr.write(proc.stderr or "")
        fail(f"command failed ({proc.returncode}): {printable}")
    return proc


def have(tool: str) -> bool:
    return shutil.which(tool) is not None


def make_program() -> str:
    """GNU make.  On the BSDs and macOS `gmake` may be the real one."""
    for cand in ("gmake", "make"):
        if have(cand):
            return cand
    fail("no make found", "install GNU make (macOS: Xcode CLT, Linux: build-essential)")


def cc_program() -> str:
    for cand in ("cc", "gcc", "clang"):
        if have(cand):
            return cand
    fail("no C compiler found", "install clang or gcc")


def ensure_dirs(*paths: Path) -> None:
    for p in paths:
        p.mkdir(parents=True, exist_ok=True)


def rel(p: Path) -> str:
    """Path relative to the repo root, for readable log lines."""
    try:
        return str(Path(p).resolve().relative_to(ROOT))
    except ValueError:
        return str(p)
