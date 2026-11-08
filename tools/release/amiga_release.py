#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Ronny Krueger
"""amiga_release.py -- Amiga-Archiv und Aminet-Dateien der MUI-IDE
(CMake-Ziel amiga-release, nach amigaide-m68k).

    python3 tools/release/amiga_release.py [--date TT.MM.JJJJ]

Erwartet die fertig übersetzte ide/amiga/bin/amigaide und baut daraus
unter build/:

  amiga-dist/AmiDevIDE/        Installationsschublade
      AmigaIDE/AmigaIDE        Programm (+ Icon), AmigaIDE.guide (+ Icon)
      Install-AmiDevIDE(.shell), Uninstall-AmiDevIDE, ReadMe,
      LICENSE, THIRD-PARTY-NOTICES
  release/AmiDevIDE-<v>.lha    für GitHub, mit Version im Namen
  release/AmiDevIDE-<v>.readme
  release/aminet-<v>/AmiDevIDE.lha + .readme   für Aminet, ohne Version

Prüft vorher: Version zweiteilig (aide_version.h), $VER im Programm mit
dieser Version und dem Datum, Handbuch (check-guide.py), keine @...@-
Platzhalter mehr, Aminet-Readme (aminet-readme-check.awk), danach das
Archiv mit "lha t", falls Lhasa installiert ist.

Version und Datum: Version aus ide/core/include/aide/aide_version.h, Datum
heute (wie der Build-Stempel der IDE). Bauen und Archiv am selben Tag."""
import argparse
import datetime
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
TOOLS = os.path.join(ROOT, "tools", "release")
INST = os.path.join(ROOT, "install", "amiga")
BUILD = os.path.join(ROOT, "build")
PROJECT = "AmiDevIDE"


def die(msg):
    print(f"amiga_release: {msg}", file=sys.stderr)
    sys.exit(1)


def run(cmd, **kw):
    r = subprocess.run(cmd, **kw)
    if r.returncode:
        die(f"fehlgeschlagen: {' '.join(cmd)}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--date", default=datetime.date.today().strftime("%d.%m.%Y"))
    args = ap.parse_args()

    vh = open(os.path.join(ROOT, "ide/core/include/aide/aide_version.h")).read()
    m = re.search(r'#define AMIGAIDE_VERSION_STRING "(\d+)\.(\d+)"', vh)
    if not m:
        die("AMIGAIDE_VERSION_STRING ist nicht zweiteilig (version.revision)")
    major, revn = int(m.group(1)), int(m.group(2))
    version = f"{major}.{revn}"

    prog = os.path.join(ROOT, "ide/amiga/bin/amigaide")
    if not os.path.isfile(prog):
        die(f"{prog} fehlt (erst cmake --build build --target amigaide-m68k)")
    want = f"$VER: AmigaIDE {version} ({args.date})".encode()
    if want not in open(prog, "rb").read():
        die(f"{prog} trägt nicht \"{want.decode()}\" -- neu bauen "
            "(amigaide-m68k) oder --date angeben")

    guide = os.path.join(ROOT, "ide/amiga/AmigaIDE.guide")
    run([sys.executable, os.path.join(TOOLS, "check-guide.py"), guide])
    if f"@$VER: AmigaIDE.guide {version} ".encode() not in open(guide, "rb").read():
        die(f"AmigaIDE.guide: $VER-Zeile nennt nicht Version {version}")

    dist_top = os.path.join(BUILD, "amiga-dist")
    dist = os.path.join(dist_top, PROJECT)
    shutil.rmtree(dist_top, ignore_errors=True)
    os.makedirs(os.path.join(dist, "AmigaIDE"))
    shutil.copy2(prog, os.path.join(dist, "AmigaIDE", "AmigaIDE"))
    # the copy in the archive carries the build date in its $VER line
    gdata = open(guide, "rb").read()
    gdata = re.sub(rb"^@\$VER: AmigaIDE\.guide \S+ \([^)]*\)",
                   f"@$VER: AmigaIDE.guide {version} ({args.date})".encode(), gdata,
                   count=1, flags=re.M)
    with open(os.path.join(dist, "AmigaIDE", "AmigaIDE.guide"), "wb") as f:
        f.write(gdata)
    shutil.copystat(guide, os.path.join(dist, "AmigaIDE", "AmigaIDE.guide"))
    for f in ("Install-AmiDevIDE", "Install-AmiDevIDE.shell", "Uninstall-AmiDevIDE", "ReadMe"):
        s = open(os.path.join(INST, f), encoding="latin-1").read()
        s = (s.replace("@VERSION@", version).replace("@DATE@", args.date)
              .replace("@VER_MAJOR@", str(major)).replace("@VER_REVN@", str(revn + 1)))
        open(os.path.join(dist, f), "w", encoding="latin-1", newline="\n").write(s)
    shutil.copy2(os.path.join(ROOT, "LICENSE"), os.path.join(dist, "LICENSE"))
    parson = open(os.path.join(ROOT, "ide/core/vendor/parson.c")).read()
    lic = parson[parson.index("Parson"):parson.index("*/")].rstrip()
    open(os.path.join(dist, "THIRD-PARTY-NOTICES"), "w", encoding="latin-1", newline="\n").write(
        "AmigaIDE enthält Parson, einen JSON-Parser, unter der MIT-Lizenz:\n\n"
        + "\n".join(l[1:] if l.startswith(" ") else l for l in lic.splitlines()) + "\n")

    for dirpath, _, files in os.walk(dist):
        for f in files:
            if f == "AmigaIDE":
                continue
            data = open(os.path.join(dirpath, f), "rb").read()
            if re.search(rb"@[A-Z_]+@", data):
                die(f"Platzhalter übrig in {os.path.join(dirpath, f)}")

    run([sys.executable, os.path.join(TOOLS, "mkicon.py"), "--spec",
         os.path.join(INST, "icons.spec"), dist])

    rel = os.path.join(BUILD, "release")
    os.makedirs(rel, exist_ok=True)
    lha = os.path.join(rel, f"{PROJECT}-{version}.lha")
    run([sys.executable, os.path.join(TOOLS, "lhapack.py"), lha, dist_top, PROJECT, PROJECT + ".info"])
    readme = os.path.join(rel, f"{PROJECT}-{version}.readme")
    s = open(os.path.join(INST, "aminet.readme"), encoding="latin-1").read().replace("@VERSION@", version)
    open(readme, "w", encoding="latin-1", newline="\n").write(s)
    run(["awk", "-f", os.path.join(TOOLS, "aminet-readme-check.awk"), readme],
        env=dict(os.environ, LC_ALL="C"))
    if shutil.which("lha"):
        run(["lha", "t", lha], stdout=subprocess.DEVNULL)
        print("lha t: Archiv in Ordnung")

    aminet = os.path.join(rel, f"aminet-{version}")
    shutil.rmtree(aminet, ignore_errors=True)
    os.makedirs(aminet)
    shutil.copy2(lha, os.path.join(aminet, PROJECT + ".lha"))
    shutil.copy2(readme, os.path.join(aminet, PROJECT + ".readme"))
    for p in (lha, readme, os.path.join(aminet, PROJECT + ".lha"), os.path.join(aminet, PROJECT + ".readme")):
        print(f"{os.path.relpath(p, ROOT)}  {os.path.getsize(p)} Bytes")

    # finished archives also go to a drop folder, if it exists
    drop = os.environ.get("AMIGA_DROP", os.path.expanduser("~/Dev/Amiga"))
    if os.path.isdir(drop):
        for p in (lha, readme):
            shutil.copy2(p, drop)
        print(f"kopiert nach {drop}/")


if __name__ == "__main__":
    main()
