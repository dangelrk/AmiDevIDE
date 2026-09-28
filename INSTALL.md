# Installation — AmiDevIDE

> Alles, was nicht aus dem Code dieses Repositorys oder einer geprüften
> Download-Adresse abgeleitet werden konnte, ist mit **(ungeprüft)** markiert.
> Stand der geprüften URLs: September 2026.

AmiDevIDE ist eine AmigaOS/m68k-Entwicklungsumgebung: vbcc (C), vasm,
vlink, ACE BASIC, die zugehörigen SDKs, eine IDE (Qt 6 auf dem Host, MUI
nativ auf dem Amiga) und ein FS-UAE-Runner.

**Dieses Repository enthält keine Software Dritter.** Compiler, Assembler,
Linker, NDK, MUI, MCCs, ACE BASIC, Kickstart-ROMs und Workbench-Images werden
entweder vom Bootstrap aus den offiziellen Quellen geladen oder müssen von dir
selbst beschafft werden. Diese Anleitung sagt für jedes Teil: *was*, *woher*,
*wohin* und *in welcher Form*.

| Host | Status |
|---|---|
| macOS | vollständig verifiziert, inkl. Bootstrap aus leerem Baum |
| Linux | baut alles aus dem Quelltext, **nicht end-to-end getestet** |
| Windows (MSYS2) | baut alles aus dem Quelltext, **nicht end-to-end getestet** |
| AmigaOS 3.x nativ | nur die Compile-Regeln sind (auf dem Host) geprüft; Link, `makedir`/`delete` und die Assigns brauchen einen echten Amiga |

Auf Linux und Windows ist `acd-doctor` (siehe unten) das Werkzeug, um
herauszufinden, ob etwas fehlt.

---

## 1. Überblick: was woher kommt

| Komponente | Version (Lock) | Quelle | Wie es ins System kommt | Ziel |
|---|---|---|---|---|
| vasm | 2.0f | `http://sun.hasenbraten.de/vasm/release/vasm.tar.gz` | automatisch, aus Quelltext gebaut | `toolchain/bin/vasmm68k_mot`, `vobjdump` |
| vlink | 0.18a | `http://sun.hasenbraten.de/vlink/release/vlink.tar.gz` | automatisch, aus Quelltext gebaut | `toolchain/bin/vlink` |
| vbcc | 0.9hP2 | `http://phoenix.owl.de/tags/vbcc0_9hP2.tar.gz` | automatisch, aus Quelltext gebaut | `toolchain/bin/{vc,vbccm68k,vprof,dtgen}` |
| vbcc-Target m68k-amigaos | 2022-05-22 | `http://phoenix.owl.de/vbcc/2022-05-22/vbcc_target_m68k-amigaos.lha` | automatisch, entpackt (vorgebaute m68k-Runtime) | `toolchain/targets/m68k-amigaos/{include,lib}` |
| NDK 3.2 (Release 4) | R4 | `https://aminet.net/dev/misc/NDK3.2.lha` | automatisch, entpackt | `toolchain/sdk/NDK_3.2/` |
| MUI 3.8 Developer | 3.8 | `https://aminet.net/dev/mui/mui38dev.lha` | automatisch, entpackt | `toolchain/sdk/MUI_3.8/` |
| ACE BASIC | 3.0.1 (fest) | `https://github.com/mdbergmann/ACEBasic/archive/refs/tags/3.0.1.tar.gz` | automatisch, entpackt nach `var/src/acebasic/`, mit `compilers/acebasic/patches/` gepatcht, aus Quelltext gebaut (§4) | `toolchain/bin/{ace,yap,bas}`, `toolchain/sdk/ACE/` |
| MCC NList | 0.128 | `https://github.com/amiga-mui/nlist/releases/download/0.128/MCC_NList-0.128.lha` | automatisch, entpackt (nur `Developer/C/include`, `Developer/Autodocs`) | `toolchain/sdk/MCC/` |
| MCC TextEditor | 15.56 | `https://github.com/amiga-mui/texteditor/releases/download/15.56/MCC_TextEditor-15.56.lha` | automatisch, entpackt, mit NList zusammengeführt | `toolchain/sdk/MCC/` |
| ReqTools Developer | 2.9 | `https://aminet.net/util/libs/ReqToolsDev.lha` | automatisch, entpackt (`include`, `fd`, `doc`) | `toolchain/sdk/reqtools/` |
| regex, Roadshow, AmiSSL, SQLite3, PosixLib | – | keine feste Download-Adresse im Code | **manuell** (§5) | `toolchain/sdk/<Name>/` |
| Kickstart-ROM, Workbench-HDF | – | **nur du selbst** (z. B. Amiga Forever) | **manuell** (§7) | `var/emu/...` |
| FS-UAE | – | `https://fs-uae.net/download/` | manuell, optional | Systeminstallation |
| Qt 6 | 6.11.1 auf macOS verifiziert | `https://www.qt.io/download-qt-installer-oss` oder Paketmanager | manuell, nur für die IDE | beliebig, per `CMAKE_PREFIX_PATH` |

Versionen und SHA-256 der automatisch geladenen Archive stehen in
`bootstrap/sources.lock.json`. Die Quelldefinitionen stehen in
`bootstrap/sources.toml`.

### Lizenzen der geladenen Teile (Kurzfassung)

- **vbcc** (© Volker Barthelmann), **vasm** (© Volker Barthelmann, Frank
  Wille), **vlink** (© Frank Wille): Archive dürfen *unverändert*
  weitergegeben und *nicht-kommerziell* genutzt werden; kommerzielle Nutzung
  ist erlaubt, wenn Ziel m68k/AmigaOS ist. Sonst schriftliche Zustimmung
  nötig. (Wortlaut in `doc/vbcc.texi`, `doc/vasm.texi`, `vlink.texi` der
  Quellarchive.)
- **NDK 3.2**: © Hyperion Entertainment CVBA, über Aminet verteilt.
- **MUI 3.8 Developer**: © 1992–97 Stefan Stuntz; frei weitergebbar, solange
  die Copyrights erhalten bleiben, nicht gewinnorientiert verkaufbar.
- **MCC NList, MCC TextEditor**: LGPL 2.1 (amiga-mui-Projekt).
- **ReqTools 2.9**: © 1991–94 Nico François, 1995–97 Magnus Holmgren;
  frei weitergebbar.
- **ACE BASIC**: GPL (Original 1998 GPL v2 von David Benn; das aktuelle
  Upstream-Repository `mdbergmann/ACEBasic` enthält seit 06/2026 eine GPL-v3-`LICENSE`).
- **Kickstart/Workbench**: urheberrechtlich geschützt, **niemals** weitergeben.

Deshalb liegt nichts davon im Repository und `toolchain/` sowie `var/` sind
in `.gitignore` eingetragen.

---

## 2. Verzeichnislayout (was der Bootstrap erzeugt)

```
AmiDevIDE/
├── bootstrap/            Python-Skripte (im Repo)
├── templates/            Vorlagen für generierte Dateien (im Repo)
├── ide/                  IDE-Quelltext (im Repo)
├── compilers/acebasic/   nur die ACE-Patches (im Repo), siehe §4
├── emulator/             FS-UAE-Runner (im Repo)
│
├── var/                  GENERIERT, gitignored   (überschreibbar: ACD_VAR)
│   ├── downloads/        Original-Archive, unverändert (nicht entpacken!)
│   │   ├── vasm.tar.gz
│   │   ├── vlink.tar.gz
│   │   ├── vbcc0_9hP2.tar.gz
│   │   ├── vbcc_target_m68k-amigaos.lha
│   │   ├── NDK3.2.lha
│   │   ├── mui38dev.lha
│   │   └── ACEBasic-3.0.1.tar.gz
│   ├── src/{vbcc,vasm,vlink}/   entpackte Quellen, hier wird gebaut
│   ├── src/acebasic/     ACE 3.0.1 + Patches, als Git-Repository (Tag `upstream`)
│   ├── stage/            Zwischenablage beim Entpacken
│   └── emu/
│       ├── kickstarts/   amiga-os-310-a1200.rom   (DU lieferst)
│       ├── hdf/          workbench-311.hdf        (DU lieferst)
│       └── adf/          *.adf, optional          (DU lieferst)
│
└── toolchain/            GENERIERT, gitignored — das ist $VBCC  (überschreibbar: ACD_TOOLCHAIN)
    ├── bin/              vc vbccm68k vasmm68k_mot vlink vobjdump vprof dtgen
    │                     ace yap bas   amigaide (+ AmigaIDE.app auf macOS)
    ├── config/           aos68k aos68km aos68kr vc.config ace68k
    ├── targets/m68k-amigaos/{include,lib}
    ├── sdk/
    │   ├── NDK_3.2/      Include_H Include_I lib FD SFD Autodocs …
    │   ├── MUI_3.8/      Developer/{C/Include,FD,Autodocs,…}
    │   ├── ACE/          lib bmaps include icons submods quickref.txt
    │   ├── MCC/          Developer/{C/include/mui,Autodocs/MCC}   (NList + TextEditor)
    │   ├── reqtools/     include fd doc
    │   └── regex/ Roadshow/ AmiSSL-v5/ SQLite3/ PosixLib/   (manuell, §5)
    ├── fs-uae/development.fs-uae
    ├── project-template/ Makefile*
    ├── shared/           wird in FS-UAE als SHARED: gemountet
    ├── setenv.sh
    └── setenv.ps1
```

Beide Wurzeln lassen sich per CMake verschieben:
`-DACD_TOOLCHAIN_PREFIX=/pfad` und `-DACD_VAR_DIR=/pfad`
(bzw. Umgebungsvariablen `ACD_TOOLCHAIN` / `ACD_VAR` beim direkten Aufruf der
Python-Skripte).

---

## 3. Host-Voraussetzungen

Gemeinsam für alle Hosts (aus `CMakeLists.txt` und `bootstrap/` abgeleitet):

| Werkzeug | Mindestversion | Wofür |
|---|---|---|
| Python | **3.11** (wegen `tomllib`; CMake prüft `Python3 3.11`) | Bootstrap |
| CMake | **3.21** | Superbuild |
| C-Compiler (`cc`, `gcc` oder `clang`) | C99 | vbcc, vasm, vlink, ACE |
| GNU make (`gmake` oder `make`) | – | alle Makefiles |
| LHA-Entpacker: `lha`, `lhasa`, `7z`, `7zz` **oder** `pip3 install lhafile` | – | `.lha`-Archive |
| `curl` oder `wget` | – | Fallback, falls Pythons TLS keine CA-Liste hat |
| `git` | – | Clone; **Pflicht** für das Einspielen der ACE-Patches (`git am`) |
| C++20-Compiler + Qt 6 (Widgets, Concurrent, Test) | Qt 6.11.1 verifiziert | nur für die Qt-IDE und die Tests |
| FS-UAE | – | optional, zum Testen im Emulator |

### macOS (verifiziert)

```sh
xcode-select --install                 # clang, make, git
brew install cmake python@3.12 lhasa   # oder: pip3 install lhafile
pip3 install certifi                   # optional: TLS-CA für python.org-Pythons
```

Qt 6 über den Online-Installer (`https://www.qt.io/download-qt-installer-oss`),
z. B. nach `~/Qt/6.11.1/macos`, oder `brew install qt` (ungeprüft).
FS-UAE: `https://fs-uae.net/download/` → `FS-UAE.app` nach `/Applications`.

### Linux (nicht end-to-end getestet)

Debian/Ubuntu (Paketnamen ungeprüft):

```sh
sudo apt install build-essential cmake python3 git lhasa curl \
                 qt6-base-dev libgl1-mesa-dev fs-uae
```

Fedora (Paketnamen ungeprüft):

```sh
sudo dnf install gcc gcc-c++ make cmake python3 git lhasa curl qt6-qtbase-devel fs-uae
```

Python muss ≥ 3.11 sein (`python3 --version`). FS-UAE wird unter
`/usr/bin/fs-uae`, `/usr/local/bin/fs-uae`, `/snap/bin/fs-uae` oder im `PATH`
gesucht.

### Windows (nicht end-to-end getestet)

Alles läuft in **MSYS2** (`https://www.msys2.org/`), Shell „MINGW64“:

```sh
pacman -S --needed mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake make git \
          mingw-w64-x86_64-python mingw-w64-x86_64-p7zip
# Qt 6 für MinGW (ungeprüft):
pacman -S mingw-w64-x86_64-qt6-base
```

vasm/vlink werden auf Windows mit `Makefile.Cygwin` gebaut (siehe
`bootstrap/acd_build_tools.py`). FS-UAE wird unter
`C:\Program Files\FS-UAE\Windows\x86-64\fs-uae.exe` (bzw. den anderen in
`emulator/acd_emu.py` gelisteten Pfaden) oder im `PATH` gesucht. Nach dem
Bauen der IDE muss `windeployqt` von Hand laufen (siehe `ide/cmake/Deploy.cmake`).

---

## 4. ACE BASIC (automatisch, mit Patches)

ACE BASIC ist GPL-Software von Dritten und liegt **nicht** in diesem
Repository. Im Repository liegen nur unsere Änderungen, als Patch-Serie unter
`compilers/acebasic/patches/`. Sie gilt für genau ein Release:

| | |
|---|---|
| Upstream | `https://github.com/mdbergmann/ACEBasic`, Tag `3.0.1` |
| Archiv | `https://github.com/mdbergmann/ACEBasic/archive/refs/tags/3.0.1.tar.gz` → `var/downloads/ACEBasic-3.0.1.tar.gz` (nicht entpacken, nicht umbenennen) |
| SHA-256 | `8a38419e35c31cf7474385384940166c4e97f2fda36c46501e5d7d40900a556f` |

Der Bootstrap erledigt alles (`acd-toolchain` bzw. `acd-ace`):

1. `acd-fetch` lädt das Archiv.
2. `acd-ace-src` prüft die SHA-256, entpackt nach `var/src/acebasic/` und
   wendet die Patches mit `git am` an. Dafür wird **`git`** gebraucht.
3. `acd-ace` baut `ace`, `yap` und `bas` für den Host und installiert sie
   nach `toolchain/`.

Das Aminet-Archiv `dev/basic/ace-basic.lha` passt **nicht**: Die Patches
brauchen genau den Baum des GitHub-Tags. Der manuelle Weg und die Arbeit an
den Patches stehen in `compilers/acebasic/README.md`, das Portieren auf ein
neues ACE-Release in `compilers/acebasic/UPDATING.md`.

Ohne ACE funktioniert alles andere (C/Assembler); nur BASIC-Projekte in der
IDE und die Tools `ace`, `yap`, `bas` fehlen dann (`acd-doctor` meldet das).

---

## 5. SDKs ohne automatischen Download (manuell)

`bootstrap/acd_install_sdk.py` installiert NDK 3.2, MUI 3.8, MCC (NList und
TextEditor) und reqtools — alles, was die MUI-IDE (`amigaide-m68k`) zum
Übersetzen braucht. Die folgenden SDKs brauchst du nur, wenn du sie nutzt.
Lege sie so ab, wie in der Spalte „Erwartetes Layout“ angegeben (geprüft wird
der jeweils erste Pfad durch `acd-doctor`):

| Verzeichnis unter `toolchain/sdk/` | Erwartetes Layout | Mögliche Quelle |
|---|---|---|
| `MCC/` (weitere Klassen) | `Developer/C/include/mui/*.h`, `Developer/Autodocs/MCC/*.doc` | BetterString, TheBar usw. installiert der Bootstrap nicht; ihre `Developer/`-Bäume von GitHub `amiga-mui` hineinkopieren |
| `regex/` | `include/{clib,libraries,pragmas,proto}/…`, `fd/regex_lib.fd` | Aminet, z. B. `util/libs/regex_ranieri` (ungeprüft, welches Paket das Original ist). Die MUI-IDE ruft `regex.library` über eigene Stubs auf und braucht die Header zum Übersetzen nicht |
| `Roadshow/` | `include/`, `netinclude/`, `sfd/` | Roadshow-SDK (ungeprüft); NDK 3.2 enthält unter `SANA+RoadshowTCP-IP/` ebenfalls `netinclude` |
| `AmiSSL-v5/` | `Developer/include/`, `Developer/lib/AmigaOS3/` | `https://github.com/jens-maus/amissl/releases/download/5.27/AmiSSL-5.27-SDK.lha` (Layout ungeprüft) |
| `SQLite3/` | `include/`, `lib/` | Aminet `biz/dbase/sqlite3` (ungeprüft) |
| `PosixLib/` | `include/` | Aminet `dev/c/vbcc_PosixLib` (ungeprüft) |

Hast du eine ältere Toolchain im alten `~/Dev/amiga`-Layout, übernimmt
`acd-seed` diese SDKs (und ROMs/HDFs) automatisch:

```sh
python3 bootstrap/acd_seed_from_legacy.py --from /pfad/zum/alten/baum [--dry-run]
```

Erwartet wird dort: `<baum>/sdk/<Name>/`, `<baum>/kickstarts/*.rom`,
`<baum>/hdf/workbench*.hdf`, `<baum>/adf/*.adf`.

### 5.1 Eigene SDKs

Jedes weitere SDK beschreibt sich selbst mit einer Datei `AmiDevSDK.json` in
seinem Verzeichnis. Alle Pfade darin sind relativ zu diesem Verzeichnis:

```json
{
  "name": "Foo Lib",
  "version": "1.2",
  "kind": "other",
  "includeDirs": ["include"],
  "asmIncludeDirs": [],
  "libDirs": ["lib"],
  "autodocDirs": ["Autodocs"],
  "autodocFiles": [],
  "sfdDirs": ["SFD"],
  "suggestedLibs": ["foo"]
}
```

Pflicht sind `name` und `version`; `kind` ist `ndk`, `mui` oder `other`
(Standard), `"."` steht für das SDK-Verzeichnis selbst. Die Qt-IDE findet
solche SDKs unter `toolchain/sdk/*/`, die MUI-IDE unter `SDK:*/`. Dasselbe
Verzeichnis funktioniert also unverändert auf beiden Seiten.

**Name und Version sind die Identität.** Ein Projekt speichert ein SDK als Id,
die beide IDEs gleich aus Name und Version bilden: klein geschrieben, alles
außer `a-z`, `0-9` und `.` wird zu `-`, also „Foo Lib“ + „1.2“ →
`foo-lib-1.2`. Verglichen wird ohne Rücksicht auf Groß- und Kleinschreibung.
Eingebaute SDKs gewinnen bei gleicher Id.

In der Qt-IDE lässt sich ein SDK auch unter *Settings → SDKs* von Hand anlegen,
mit Name und Version (die Id wird angezeigt, nicht eingegeben). Das gilt nur
auf diesem Rechner; für den Amiga braucht es die `AmiDevSDK.json`.

Nennt ein Projekt ein SDK, das nicht installiert ist, melden beide IDEs das
beim Öffnen und am Anfang jedes Builds. In den Projekteinstellungen steht es
mit „(not installed)“ bzw. „(nicht installiert)“ und bleibt angehakt, bis man
das Häkchen entfernt.

---

## 6. Host: Schritt für Schritt

### 6.1 Klonen

```sh
git clone https://github.com/dangelrk/AmiDevIDE.git
cd AmiDevIDE
```

Pfad **ohne Leerzeichen** wählen (vc ruft seine Tools direkt, nicht über eine
Shell, auf; ein Pfad mit Leerzeichen in den generierten Configs ist
ungetestet).

### Alles in einem Schritt: `install.sh`

```sh
./install.sh                              # sucht Qt selbst
./install.sh --qt ~/Qt/6.11.1/macos       # oder mit Pfad
```

Erledigt §6.2 bis §6.8 in einem Lauf: prüft die Voraussetzungen aus §3 und
bietet an, fehlende (CMake, Python, lhasa) per Homebrew zu installieren
(`--yes` ohne Rückfrage). Es sucht Qt 6 im CMake-Cache, in `QTDIR`/
`CMAKE_PREFIX_PATH`, unter `~/Qt/6.*/macos`, in Homebrew und über `qmake` und
fragt nach dem Pfad, wenn nichts davon passt. Dann konfiguriert es mit Qt-IDE,
MUI-IDE und Tests und baut Toolchain, `acd-doctor`, Qt-IDE, `ctest` und
`amigaide-m68k`. Bei einem Fehler nennt es die Ursache und den nächsten
Schritt; die Protokolle liegen unter `var/log/install/`. Jeder Schritt ist
wiederholbar, nach einer Korrektur also einfach erneut starten. Der Emulator
(§7) gehört nicht dazu. Verifiziert auf macOS; auf Linux läuft derselbe Ablauf,
installiert aber nichts selbst.

Die Einzelschritte:

### 6.2 Konfigurieren

```sh
cmake -B build                                            # nur Toolchain
cmake -B build -DCMAKE_PREFIX_PATH=$HOME/Qt/6.11.1/macos  # mit Qt-IDE
```

Optionen: `ACD_TOOLCHAIN_PREFIX`, `ACD_VAR_DIR`, `ACD_BUILD_IDE_QT` (ON,
schaltet sich ohne Qt 6 ab), `ACD_BUILD_IDE_AMIGA` (OFF), `ACD_BUILD_TESTS`
(ON), `ACD_OFFLINE` (OFF).

### 6.3 Toolchain bauen

```sh
cmake --build build --target acd-toolchain
```

Das führt der Reihe nach aus:

1. `acd-fetch` — lädt die zehn Archive aus §1 nach `var/downloads/`
2. `acd-tools` — baut vbcc (inkl. automatisch beantwortetem `dtgen`), vasm, vlink → `toolchain/bin/`
3. `acd-sdk` — entpackt vbcc-Target, NDK 3.2, MUI 3.8, MCC, reqtools → `toolchain/targets/`, `toolchain/sdk/`
4. `acd-ace-src` — ACE 3.0.1 prüfen, nach `var/src/acebasic/` entpacken, Patches anwenden
5. `acd-ace` — `make -f Makefile-host` und `install` in `var/src/acebasic/src/make` → `toolchain/bin/{ace,yap,bas}`, `toolchain/config/ace68k`, `toolchain/sdk/ACE/`
6. `acd-config` — erzeugt `toolchain/config/aos68k*`, `setenv.sh/.ps1`, `fs-uae/development.fs-uae`, `project-template/`

Die Einzelschritte gibt es auch direkt:

```sh
python3 bootstrap/acd_fetch.py [--offline]
python3 bootstrap/acd_build_tools.py [--force] [--only vbcc,vasm,vlink]
python3 bootstrap/acd_install_sdk.py [--force]
python3 bootstrap/acd_ace.py [--force]
(cd var/src/acebasic/src/make && make -f Makefile-host && make -f Makefile-host install VBCC="$PWD/../../../../toolchain")
python3 bootstrap/acd_gen_config.py [--kickstart PFAD] [--workbench PFAD]
```

### 6.4 Offline / manueller Download

Archive **unverändert und nicht entpackt** nach `var/downloads/` legen
(Dateinamen wie in §2), dann:

```sh
cmake -B build -DACD_OFFLINE=ON
cmake --build build --target acd-toolchain
```

`python3 bootstrap/acd_fetch.py --write-docs` erzeugt `docs/SOURCES.html`
mit allen Links. Neuere Versionen prüfen:
`cmake --build build --target acd-update-check`.

### 6.5 Prüfen

```sh
cmake --build build --target acd-doctor
```

Startet jedes Tool, prüft SDKs, Configs (keine `~`/`$`), Emulator, und
kompiliert ein Hello-World für m68k. Fehlende optionale Teile (seeded SDKs,
Emulator) sind Warnungen, keine Fehler.

### 6.6 Benutzen

```sh
source toolchain/setenv.sh              # PowerShell: . toolchain\setenv.ps1
vc +aos68k -c99 -cpu=68000 -O2 hello.c -o hello
file hello                              # AmigaOS loadseg()ble executable/binary
bas hello.b                             # ACE BASIC
```

`vc` braucht `$VBCC` und `$VBCC/bin` im `PATH` — ohne das meldet es
„No config file!“. `setenv.sh` setzt beides.

### 6.7 Qt-IDE und Tests

```sh
cmake --build build                     # baut amigaide
cd build && ctest                       # 17 Test-Suites
```

`tst_autodocindex` und `tst_sfdindex` lesen echte NDK-Daten und werden
übersprungen, solange die Toolchain nicht gebootstrappt ist.

Auf macOS wird `AmigaIDE.app` per `macdeployqt` gebündelt und nach
`toolchain/bin/AmigaIDE.app` kopiert; `toolchain/bin/amigaide` ist ein
Startskript dafür. Die IDE findet die Toolchain über ihren eigenen Ort.

### 6.8 MUI-IDE für den Amiga cross-kompilieren (optional)

Braucht NDK 3.2, MUI 3.8, `sdk/MCC` und `sdk/reqtools`; alle vier installiert
`acd-toolchain` (§6.3).

```sh
cmake -B build -DACD_BUILD_IDE_AMIGA=ON
cmake --build build --target amigaide-m68k
file ide/amiga/bin/amigaide             # AmigaOS loadseg()ble executable/binary
```

---

## 7. Emulator (FS-UAE)

Kickstart-ROMs und Workbench sind urheberrechtlich geschützt und **nicht**
enthalten. Legal z. B. über Cloanto *Amiga Forever* oder durch Auslesen des
ROMs der eigenen Hardware.

Ablage (Dateinamen werden in dieser Reihenfolge gesucht):

```
var/emu/kickstarts/  amiga-os-310-a1200.rom | amiga-os-310.rom | kick31.rom
var/emu/hdf/         workbench-311.hdf | workbench-310.hdf | workbench.hdf
var/emu/adf/         *.adf (optional)
```

Zusätzlich wird in `~/FS-UAE/Kickstarts` und `~/FS-UAE/Hard Drives` gesucht.
Oder explizit:

```sh
python3 bootstrap/acd_gen_config.py --kickstart ~/roms/kick31.rom --workbench ~/images/wb311.hdf
cmake --build build --target acd-doctor
```

Die generierte Maschine: A1200, Kickstart 3.1, 2 MB Chip + 8 MB Fast,
`DH0` = Workbench-HDF, `SHARED:` = `toolchain/shared` (Live-Verzeichnis),
serielle Ausgabe nach `toolchain/shared/serial.log`.

```sh
python3 emulator/acd_emu.py --status
python3 emulator/acd_emu.py --binary hello
python3 emulator/acd_emu.py --serial
```

---

## 8. AmigaOS nativ

### 8.1 Systemvoraussetzungen

| | |
|---|---|
| CPU | **68020 oder besser** — `ide/amiga/Makefile.amiga` baut mit `-cpu=68020` (änderbar über `CPU=68000`, ungeprüft) |
| FPU | nicht nötig (`-lmieee` → `mathieeedoubbas.library` aus `LIBS:`) |
| OS | AmigaOS 3.x; die IDE öffnet `intuition.library` v37 (≥ 2.04). Mit NDK 3.2 gebaut; getestet ist kein OS-Stand (ungeprüft: 3.1 / 3.2) |
| MUI | `muimaster.library` ≥ v19 (MUI 3.8) |
| Libraries | `reqtools.library` ≥ v38, `regex.library` in `LIBS:` |
| MCCs (in `MUI:Libs/mui/`) | NList (`NList`, `NListview`, `NListtree`), `TextEditor` |
| make | GNU make für Amiga von Darren Coles (`gnu_make_amiga`, 4.4.1) |
| Speicher / Stack | ungeprüft; ACE-Upstream setzt `stack 65000` in seiner User-Startup |

### 8.2 Was wohin

| Paket | Quelle | Ziel |
|---|---|---|
| vbcc-Binaries m68k (0.9h patch2) | `https://aminet.net/dev/c/vbcc_bin_amigaos68k.lha` oder `http://phoenix.owl.de/vbcc/2022-03-23/vbcc_bin_amigaos68k.lha` | Verzeichnis, z. B. `Work:vbcc`, per Installer (legt Assign `vbcc:` an, ungeprüft) |
| vbcc-Target m68k-amigaos | `http://phoenix.owl.de/vbcc/2022-05-22/vbcc_target_m68k-amigaos.lha` | per Installer nach `vbcc:targets/m68k-amigaos/` |
| vasm/vlink für AmigaOS | im vbcc-bin-Archiv enthalten (ungeprüft), sonst `http://sun.hasenbraten.de/vasm/` bzw. `/vlink/` | `vbcc:bin/vasmm68k_mot`, `vbcc:bin/vlink` |
| NDK 3.2 R4 | `https://aminet.net/dev/misc/NDK3.2.lha` | `SDK:NDK_3.2/` (mit `Include_H`, `Include_I`, `lib`, `FD`, `SFD`, `Autodocs`) |
| MUI 3.8 Developer | `https://aminet.net/dev/mui/mui38dev.lha` | Inhalt von `MUI/` nach `SDK:MUI_3.8/` (→ `SDK:MUI_3.8/Developer/C/Include`) |
| MUI 3.8 User (Runtime) | `https://aminet.net/util/libs/mui38usr.lha` | per Installer, `MUI:` |
| MCCs Developer + Runtime | siehe §1 (GitHub `amiga-mui`) | Header nach `SDK:MCC/Developer/C/include/mui/`, Klassen nach `MUI:Libs/mui/` |
| regex.library | siehe §5 (ungeprüft) | Header nach `SDK:regex/include/`, Library nach `LIBS:` |
| reqtools | `https://aminet.net/util/libs/ReqToolsDev.lha` (Header), `https://aminet.net/util/libs/ReqToolsUsr.lha` (Library) | Header nach `SDK:reqtools/include/`, Library nach `LIBS:` |
| GNU make | `https://github.com/dmcoles/gnu_make_amiga/releases/download/4.4.1-26022026/make441-26022026.lha` oder `https://aminet.net/dev/c/make-4.4.1.lha` | `make` nach `C:` oder in einen Pfad im `PATH` |
| ACE BASIC 3.0.1 (Original, **ungepatcht**) | `https://aminet.net/dev/basic/ace-basic.lha` | z. B. `Work:ACE`, mit den Assigns `ACE:`, `ACElib:`, `ACEinclude:`, `ACEbmaps:` (§8.3) |

Erwartetes Layout der Assigns (aus `ide/amiga/Makefile.amiga`,
`templates/project-makefiles/Makefile.amigaos`, `ide/core/src/aide_makegen.c`):

```
vbcc:bin/vc  vbcc:bin/vbccm68k  vbcc:bin/vasmm68k_mot  vbcc:bin/vlink
vbcc:config/aos68k
vbcc:targets/m68k-amigaos/include   vbcc:targets/m68k-amigaos/lib
SDK:NDK_3.2/Include_H  SDK:NDK_3.2/Include_I  SDK:NDK_3.2/lib
SDK:MUI_3.8/Developer/C/Include
SDK:MCC/Developer/C/include
SDK:regex/include
SDK:reqtools/include
(optional) SDK:Roadshow/{include,netinclude}  SDK:AmiSSL-v5/Developer/{include,lib/AmigaOS3}
           SDK:SQLite3/{include,lib}          SDK:PosixLib/include
```

**ACE auf dem Amiga ist das Original.** Die Patches aus
`compilers/acebasic/` sind nur für den Host (macOS, Linux, Windows) da; auf
dem Amiga wird nichts gepatcht. Die MUI-IDE erzeugt BASIC-Makefiles für ACE,
wie es von Aminet kommt:

- `ACE:bin/ace` und `ACE:bin/yap`; Includes mit `<…>` sucht `yap` selbst in
  `ACEinclude:`.
- Gelinkt wird gegen `ACElib:startup.lib`, `ACElib:db.lib`,
  `ACElib:amiga.lib`, bei 68000 gegen `ACElib:68000/startup.lib`,
  `ACElib:68000/db.lib`, `ACElib:amiga.lib` und `ACElib:vc.lib`.
- `vasmm68k_mot` und `vlink` kommen aus `vbcc:bin/`.
- **CPU 68000:** Das Original-`ace` kennt dafür keinen Schalter, nur
  `OPTION 2-` im Quelltext. Das Makefile stellt diese Zeile automatisch vor
  den vorverarbeiteten Text (`Echo` und `Join`); der Quelltext und die
  `.aproj` bleiben unverändert (ungeprüft auf echter Hardware).
- **Fehlerzeilen:** Das Original-`yap` kennt kein `-g`. `ace` meldet Fehler
  deshalb mit der Zeilennummer im vorverarbeiteten Text, ohne Dateinamen. Die
  IDE zeigt sie an, kann sie aber keiner Quelldatei zuordnen.

Dieselbe `.aproj` baut auf dem Host mit dem gepatchten ACE; welches Verhalten
gilt, entscheidet allein das von der IDE erzeugte Makefile.

### 8.3 S:User-Startup

Beispiel (Pfade an dein System anpassen; `vbcc:`-Zeilen legt ggf. schon der
vbcc-Installer an — dann nicht doppelt eintragen):

```
;BEGIN AmiDevIDE
Assign vbcc: Work:vbcc
Path vbcc:bin ADD
Assign SDK: Work:SDK
Assign ACE: Work:ACE
Assign ACElib: ACE:lib
Assign ACEbmaps: ACE:bmaps
Assign ACEinclude: ACE:include
Path ACE:bin ADD
Stack 65536
;END AmiDevIDE
```

Zu den vbcc-eigenen Assigns (`vincludeos3:`, `vlibos3:` o. ä., auf die
`vbcc:config/aos68k` verweist): ungeprüft — der vbcc-Installer trägt sie ein.
Das Template `Makefile.amigaos` setzt außerdem `VBCC=vbcc:` und
`VBCCCONF=vbcc:config/aos68k` beim Aufruf.

Die Pfade `SDK:`/`vbcc:` sollten **keine Leerzeichen** enthalten.

### 8.4 AmigaIDE (MUI) nativ bauen

`ide/amiga/Makefile.amiga` setzt `ROOT = /AmigaIDE`. In AmigaDOS heißt ein
führendes `/` „Elternverzeichnis“. Daher:

1. Den Inhalt von `ide/` auf den Amiga kopieren, in ein Verzeichnis **namens
   `AmigaIDE`**, z. B. `Work:AmigaIDE` (mit `core/`, `amiga/`).
2. In der Shell:

   ```
   CD Work:AmigaIDE
   make -f amiga/Makefile.amiga app
   ```

   Sicherer (ungeprüft) ist, `ROOT` explizit zu setzen:
   `make -f amiga/Makefile.amiga ROOT=Work:AmigaIDE app`

Ziele: `core-objects` (nur Kern, ohne Link), `app` (→ `amiga/bin/amigaide`),
`clean`. Nur die Compile-Regeln sind geprüft; Link, `makedir`/`delete` und
die Assigns sind auf echter Hardware **ungeprüft**.

Alternativ auf dem Host cross-kompilieren (§6.8) und nur
`ide/amiga/bin/amigaide` auf den Amiga kopieren.

### 8.5 Einstellungen der IDE auf dem Amiga

Die MUI-IDE speichert ihre Einstellungen in `ENVARC:AmigaIDE/`:

| Variable | Standard |
|---|---|
| `AmigaIDE/make` | `make` |
| `AmigaIDE/sdkroot` | `SDK:` |
| `AmigaIDE/vbccroot` | `vbcc:` |

---

## 9. Fehlerbilder

| Meldung | Ursache |
|---|---|
| `No config file!` von `vc` | `$VBCC` nicht gesetzt → `source toolchain/setenv.sh` |
| `cmake -B build`: „No LHA extractor found“ | kein `lha`/`lhasa`/`7z`/`7zz` im `PATH` und kein `lhafile` für CMakes Python → `brew install lhasa` bzw. `apt install lhasa`, dann `cmake` erneut. Sind die SDKs schon installiert, ist es nur eine Warnung |
| `no working LHA extractor` | `pip3 install lhafile` oder `lhasa`/`7z` installieren |
| `make: …/vc: No such file or directory` in der IDE | Toolchain verschoben; IDE-Einstellungen zeigen ins Leere (werden beim Start repariert, sonst in den Einstellungen korrigieren) |
| macOS: `Library not loaded: @rpath/QtWidgets.framework` | nacktes `amigaide`-Binary kopiert — immer über `toolchain/bin/amigaide` (Startskript) bzw. die `.app` starten |
| `acd-ace-src`: „does not match the pinned release“ | falsches oder beschädigtes ACE-Archiv in `var/downloads/` — löschen und `acd-fetch` erneut laufen lassen |
| `acd-ace-src`: „the ACE patch series does not apply“ | Patches passen nicht zum Archiv, siehe `compilers/acebasic/UPDATING.md` |
| `acd-ace-src`: „has uncommitted changes“ | in `var/src/acebasic/` wurde etwas geändert — committen und als Patch exportieren oder `python3 bootstrap/acd_ace.py --force` |
