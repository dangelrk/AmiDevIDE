# AmigaOS Cross-Compiling & Testing — Anleitung

Diese Datei richtet sich an einen anderen Chat/Agenten, der an einem
Amiga-Projekt arbeitet und dafür m68k-AmigaOS-Code
**von einem Host-Rechner aus** (macOS/Linux/Windows) bauen und in einem
Emulator testen soll — ohne echten Amiga.

Die Toolchain wird **generiert**, nicht mitgeliefert. Sie entsteht unter
`AmiDevIDE/toolchain/` — das ist der Pfad, auf den `$VBCC` zeigt, auf
allen drei Betriebssystemen gleich.

```sh
cmake -B build
cmake --build build --target acd-toolchain    # holt, baut, installiert
cmake --build build --target acd-doctor       # prüft das Ergebnis
```

Getestet und verifiziert: **macOS** (vollständig, inkl. Bootstrap von Null).
Linux/Windows sind strukturell identisch und bauen alles aus dem Quelltext,
sind aber nicht selbst gegengetestet — `acd-doctor` sagt dir, ob etwas fehlt.
Auf Windows brauchst du MSYS2 mit `mingw-w64-x86_64-gcc` und `make`.

## 1. Was ist das

Ein Cross-Compiler für AmigaOS/m68k auf Basis von **vbcc** (Compiler),
**vasm** (Assembler) und **vlink** (Linker), plus den nötigen SDKs:

- `targets/m68k-amigaos/` — vbcc-C-Runtime (include + lib) für m68k-amigaos
- `sdk/NDK_3.2` — Amiga NDK (Systemheader/-libs).
  **Achtung: 3.2 ist NEUER als 3.9.** NDK 3.9 ist entfallen.
- `sdk/MUI_3.8` — MUI GUI-Toolkit. MUI 5.0 ist entfallen: es liefert nur
  GCC-`.a`-Libraries, die vlink nicht linken kann.
- `sdk/MCC`, `sdk/regex`, `sdk/reqtools` — MUI-Zusatzklassen und die beiden
  Libraries, gegen die die MUI-Variante der IDE linkt
- `sdk/Roadshow` — TCP/IP-Stack-Header (`bsdsocket`, `netinclude`)
- `sdk/AmiSSL-v5` — TLS/SSL
- `sdk/SQLite3`, `sdk/PosixLib`
- `bin/` — die Compiler-Binaries (`vc`, `vbccm68k`, `vasmm68k_mot`, `vlink`,
  `vobjdump`, `vprof`, `dtgen`, `ace`, `yap`, `bas`). **Alle aus dem
  Quelltext gebaut**, für die Plattform, auf der du bootstrappst — es gibt
  keine vorgefertigten Binaries mehr und damit auch keine Plattform, für die
  eines fehlt (ACE gab es früher nur für macOS).
- `fs-uae/development.fs-uae` — die generierte Emulator-Maschine. Eine Datei
  für alle drei Betriebssysteme; die Pfade darin werden beim Bootstrap
  eingesetzt.
- `shared/` — das Austauschverzeichnis, das FS-UAE als `SHARED:` mountet.
- Kickstart-ROM und Workbench-Image liegen unter `var/emu/` und musst du
  selbst beisteuern — siehe `emulator/images/README.md`.

## 2. Environment aufsetzen

Das generierte Script sourcen (liegt im Toolchain-Root):

```sh
source AmiDevIDE/toolchain/setenv.sh      # macOS, Linux, MSYS2
. AmiDevIDE\toolchain\setenv.ps1          # Windows PowerShell
```

Das setzt `VBCC`, `PATH` (inkl. `$VBCC/bin`) und die `*_INC`/`*_LIB`-
Variablen der SDKs. Eine Datei statt drei fast identischer, und sie wird
generiert — sie kann also nicht auf einen Pfad zeigen, den es nicht gibt.

**Der alte Fallstrick ist erledigt.** Früher enthielten die vc-Configs
`~/Dev/amiga/...`, das nie expandiert wurde (`vc` `exec`t seine Tools direkt,
ohne Shell), weshalb man `-I`/`-L` immer von Hand mitgeben musste. Die Configs
werden jetzt beim Bootstrap mit absoluten Pfaden erzeugt. Verifiziert:

```sh
vc +aos68k -c99 -cpu=68000 -O2 main.c -o hello    # ohne jedes -I/-L
```

`acd-doctor` prüft explizit, dass in keiner erzeugten Config ein `~` oder `$`
stehen geblieben ist.

**Was du weiterhin brauchst:** `$VBCC` muss gesetzt sein. `vc` löst `+aos68k`
zu `$VBCC/config/aos68k` auf und ruft `vbccm68k`/`vasmm68k_mot`/`vlink` über
`PATH` auf. Genau das macht `setenv.sh`.

## 3. Schnelltest (Sanity Check, bevor am echten Projekt gebaut wird)

```sh
source AmiDevIDE/toolchain/setenv.sh
mkdir -p /tmp/amigatest && cd /tmp/amigatest
cat > main.c <<'EOF'
#include <stdio.h>
int main(void) { printf("Hello Amiga!\n"); return 0; }
EOF
vc +aos68k -c99 -cpu=68000 -O2 main.c -o hello
file hello   # -> "AmigaOS loadseg()ble executable/binary"
```

Wenn das funktioniert, ist die Toolchain korrekt eingerichtet.

## 4. Neues Projekt mit dem mitgelieferten Makefile-Template

Für ein frisches Projekt liegt unter `toolchain/project-template/` ein
fertiges (und beim Bootstrap erzeugtes),
plattform-erkennendes Build-System:

1. Diese 7 Dateien **flach** (nicht in einen Unterordner!) ins
   Projekt-Root kopieren: `Makefile`, `Makefile.rules`,
   `Makefile.platform`, `Makefile.windows`, `Makefile.linux`,
   `Makefile.macos`, `Makefile.amigaos`.
   (Grund: `Makefile.platform` lädt `include Makefile.macos` etc. —
   das wird relativ zum `make`-Arbeitsverzeichnis aufgelöst, nicht
   relativ zur includenden Datei. Liegen die Dateien in einem
   Unterordner, bricht das.)
2. Im Kopf von `Makefile` anpassen: `PROJECT`, `VERSION`, `C_SOURCES`,
   `ASM_SOURCES`, `CPU` (z. B. `68000`), `OPT_LEVEL`, `DEBUG`,
   `PROJECT_INCLUDES`, `EXTRA_LIBS` (SDK-Libs ohne `-l`-Prefix, z. B.
   `mui amissl`).
3. Bauen mit `make bin/<PROJECT>`.

   **Nicht** einfach nur `make` bzw. `make all` aufrufen, solange kein
   ADF-Packaging eingerichtet ist: Das Default-Target versucht zusätzlich
   ein `dist/<PROJECT>.adf` zu erzeugen (via `xdftool` aus dem Python-
   Paket `amitools`, das hier standardmäßig **nicht** installiert ist —
   `pip3 install amitools`), plus ein projekteigenes `adf/empty.adf`.
   Fehlt eines von beidem, bricht `make all` am ADF-Schritt ab, obwohl
   das eigentliche Binary schon fertig gebaut wurde.
4. `make clean` / `make rebuild` / `make info` / `make help` funktionieren
   wie erwartet (siehe `Makefile.rules`).

Die Plattformerkennung erfolgt automatisch über `uname`/`$OS` in
`Makefile.platform` — dasselbe `Makefile` funktioniert unverändert auf
macOS, Linux, Windows (MSYS2) und nativ auf AmigaOS (dort über AmigaDOS-
Pfade wie `vbcc:` statt Host-Pfaden, siehe `Makefile.amigaos`).

## 5. Bestehendes Projekt mit eigenem Makefile cross-kompilierbar machen

Viele bestehende Amiga-Projekte haben ein `Makefile.amiga`, das für den
**nativen** Build auf einem echten/emulierten Amiga gedacht ist:

```make
CC = vc
ROADSHOW_INC = Dev:SDK/Roadshow/netinclude
AMISSL_INC   = Dev:SDK/AmiSSL/Developer/include
...
clean:
	delete quiet #?.o $(TARGET)
```

Das läuft so **nicht** auf dem Host, weil:
- `Dev:SDK/...` ein AmigaDOS-Assign ist, keine Host-Pfadangabe,
- `delete`, `copy`, `makedir quiet` AmigaDOS-Shell-Befehle sind (auf
  dem Host gibt es die nicht),
- `vc` auf dem Host nur über `$VBCC/bin/vc` gefunden wird, nicht einfach
  über `PATH`, solange das Environment nicht gesourced wurde.

**Vorgehen:** eine zweite Variante des Makefiles anlegen (z. B.
`Makefile.amiga-cross` oder per `ifeq ($(PLATFORM),...)`-Block ergänzen),
die dieselben Targets/Objektlisten wiederverwendet, aber:

| Nativ (`Dev:SDK/...`, AmigaDOS)      | Cross (Host)                                          |
|---------------------------------------|--------------------------------------------------------|
| `Dev:SDK/Roadshow/netinclude`         | `$(VBCC)/sdk/Roadshow/netinclude`                       |
| `Dev:SDK/AmiSSL/Developer/include`    | `$(VBCC)/sdk/AmiSSL-v5/Developer/include`               |
| `Dev:SDK/MUI/C/Include`               | `$(VBCC)/sdk/MUI_3.8/Developer/C/Include`               |
| `CC = vc`                             | `CC = $(VBCC)/bin/vc` (oder `vc`, wenn Env gesourced)   |
| fehlende `-I`/`-L` für Runtime        | nicht mehr nötig — die generierte vc-Config bringt sie mit (siehe §2) |
| `delete quiet #?.o $(TARGET)`         | `rm -f *.o $(TARGET)`                                   |
| `copy $(HANDLER) L:`                  | entfällt beim Cross-Build (Installation passiert auf dem Amiga/im Emulator, nicht beim Cross-Compile) |

Alles andere (CFLAGS wie `-O2 -cpu=68000 +aos68k`, Objektlisten,
Linkreihenfolge, projektspezifische Flags wie `-nostdlib` bei
Handler-Binaries) bleibt unverändert — `vbcc`/`vlink` sind dieselben
Tools, nur der Aufrufkontext (Pfade, Shell-Befehle) ändert sich.

Falls das Projekt bereits einen `Makefile.pc` für einen *nativen*
Host-Build (z. B. mit `gcc`/`libcurl` für einen PC-Client) hat: das ist
etwas anderes als Cross-Compiling für den m68k-Amiga-Teil — beides kann
nebeneinander existieren, verwechselt es nicht.

## 6. ACE BASIC cross-kompilieren

Neben C liegt hier auch ein Cross-Compiler für **ACE BASIC** (Amiga-BASIC-
Compiler, erzeugt native m68k-Executables). Die Pipeline ist:

```
prog.b -> yap (Präprozessor) -> ace (BASIC -> 68k-Assembler)
       -> vasmm68k_mot -> vlink -> prog
```

Die letzten beiden Stufen sind dieselben Tools wie beim C-Build.

Installierte Bestandteile:

- `bin/ace`, `bin/yap`, `bin/bas` — Host-Binaries
- `config/ace68k` — Treiberkonfiguration, wird von `bas` gelesen
- `sdk/ACE/{lib,bmaps,include,icons,submods}` — Runtime-Libraries,
  Library-Offset-Maps, BASIC-Header, Submodule

### Schnelltest

```sh
source AmiDevIDE/toolchain/setenv.sh
mkdir -p /tmp/acetest && cd /tmp/acetest
cat > hello.b <<'EOF'
DEFINT a-z
PRINT "Hello Amiga!"
FOR i = 1 TO 3
  PRINT "i ="; i
NEXT i
EOF
bas hello.b
file hello   # -> "AmigaOS loadseg()ble executable/binary"
```

### bas im Überblick

```
bas [optionen] quelldatei[.b] [extra.o ...]
```

| Option | Wirkung |
|---|---|
| `-S` | nur bis zum Assembler, `.s` behalten |
| `-m` | Modul übersetzen: nur bis `.o`, kein Link |
| `-E` | Compilerfehler zusätzlich nach `ace.err` |
| `-O` | Peephole-Optimierer |
| `-2` | 68020-Code (Default; `OPTION 2-` in der Quelle schaltet auf 68000) |
| `-c` | Kommentare im erzeugten Assembler |
| `-v` | jeden Toolaufruf anzeigen |
| `-k` | Temporärverzeichnis behalten |

Zwei Dinge liest `bas` aus dem Quelltext selbst:

- `REM #using <pfad.o>` in den ersten 20 Zeilen hängt Objektdateien an den
  Link an. AmigaDOS-Assigns darin (`ace:`, `acelib:`, `aceinclude:`,
  `acebmaps:`, `acesubmods:`) werden auf die konfigurierten Host-Verzeichnisse
  abgebildet, bestehende Quellen laufen also unverändert.
- `OPTION 2-` in den ersten 50 Zeilen wählt die 68000-Runtime
  (`sdk/ACE/lib/68000/`) statt der 68020-Variante.

### Konfiguration

`bas` liest `config/ace68k` (Format `schlüssel = wert`). Überschreiben lässt
sich alles per Environment: `ACE_HOME`, `ACE_LIB`, `ACE_INC`, `ACE_BMAPS`,
`ACE_BIN`. `setenv_*.sh` setzt die ersten vier bereits.

`ace` und `yap` lassen sich auch einzeln aufrufen; sie brauchen dann
`ACE_BMAPS` (für `LIBRARY "..."`) bzw. `ACE_INC` (für `#include <...>`).

**Fallstrick:** `ace` bestimmt die Dateiendung am **ersten** Punkt im Namen,
`my.prog.b` würde also zu `my.prog.b.b`. `bas` umgeht das intern; beim
direkten `ace`-Aufruf Dateinamen ohne zusätzliche Punkte verwenden.

### In das Makefile-Template einbinden

`Makefile.rules` kennt `BASIC_SOURCES` analog zu `C_SOURCES` und
`ASM_SOURCES`. Die Einträge werden mit `bas -m` zu `.o` übersetzt und
mitgelinkt:

```make
BASIC_SOURCES = mymodule.b helper.b
```

### Neu bauen

ACE liegt nicht im Repository. Der Bootstrap lädt das Upstream-Release
3.0.1, wendet die Patches aus `compilers/acebasic/patches/` an und baut unter
`var/src/acebasic/` (Details: `compilers/acebasic/README.md`). Der Superbuild
erledigt alles:

```sh
cmake --build build --target acd-ace
```

Von Hand geht es auch:

```sh
python3 bootstrap/acd_ace.py                # entpacken + patchen
cd var/src/acebasic/src/make
make -f Makefile-host VBCC=<repo>/toolchain
make -f Makefile-host install VBCC=<repo>/toolchain
make -f Makefile-host amiga-check           # prüft, dass der native
                                            # AmigaOS-Build weiterhin baut
```

Änderungen an ACE werden in `var/src/acebasic/` committet und als Patches
zurückgeschrieben (`compilers/acebasic/README.md`, „Am Patch-Satz arbeiten“).
`amiga-check` ist nach jeder Änderung an `src/ace/c/` Pflicht: auf dem Host
ist `LONG` exakt 32 Bit, unter vbcc ist es `long` — Format-Konvertierungen
brauchen explizite `(long)`-Casts, und nur der Cross-Build fängt das.

## 7. Testen im Emulator (FS-UAE)

Die Maschine wird generiert: `toolchain/fs-uae/development.fs-uae`
(A1200, Kickstart 3.1, 2 MB Chip + 8 MB Fast RAM). **Eine** Datei für alle
drei Betriebssysteme — früher waren es drei handgepflegte, die
auseinandergelaufen waren (die Windows-Variante mountete `shared` als `DH0`
und band die Workbench-HDF gar nicht ein).

| Laufwerk | Inhalt |
|---|---|
| `DH0` | Workbench-3.1-Image aus `var/emu/hdf/` |
| `SHARED` | `toolchain/shared`, ein echtes Host-Verzeichnis |

Kickstart-ROM und Workbench-Image musst du selbst beisteuern (Lizenz) — siehe
`emulator/images/README.md`. Danach `acd-config` neu laufen lassen.

### Ablauf

```sh
python3 emulator/acd_emu.py --status           # was ist installiert?
python3 emulator/acd_emu.py --binary hello     # nach SHARED: kopieren + starten
python3 emulator/acd_emu.py --serial           # Programmausgabe mitlesen
```

`SHARED:` ist ein Verzeichnis-Mount, kein Image: läuft FS-UAE schon, wird das
Binary nur hineinkopiert und du startest es in der Amiga-Shell neu — kein
Emulator-Neustart nötig. `acd_emu.py` erkennt das und startet keine zweite
Instanz.

Die Maschine setzt außerdem `serial_port` auf `toolchain/shared/serial.log`.
Alles, was ein Programm auf die serielle Schnittstelle schreibt, landet damit
in einer Datei auf dem Host — `--serial` tailt sie, die IDE zeigt sie im
Output-Pane.

Die IDE benutzt für ihren Run-Befehl dieselbe Logik (`core/aide_emulator.c`),
die beiden können also nicht auseinanderlaufen.

## 8. Kurzreferenz der wichtigsten Variablen

```
VBCC        = <repo>/toolchain                              (generiert)
VBCC_INC    = $VBCC/targets/m68k-amigaos/include
VBCC_LIB    = $VBCC/targets/m68k-amigaos/lib
NDK32_INC   = $VBCC/sdk/NDK_3.2/Include_H
NDK32_ASM_INC = $VBCC/sdk/NDK_3.2/Include_I
NDK32_LIB   = $VBCC/sdk/NDK_3.2/lib
MUI38_INC   = $VBCC/sdk/MUI_3.8/Developer/C/Include
MCC_INC     = $VBCC/sdk/MCC/Developer/C/include
REGEX_INC   = $VBCC/sdk/regex/include
REQTOOLS_INC= $VBCC/sdk/reqtools/include
TCP_INC     = $VBCC/sdk/Roadshow/include
NET_INC     = $VBCC/sdk/Roadshow/netinclude
AMISSL_INC  = $VBCC/sdk/AmiSSL-v5/Developer/include
AMISSL_LIB  = $VBCC/sdk/AmiSSL-v5/Developer/lib/AmigaOS3
SQLITE_INC  = $VBCC/sdk/SQLite3/include
SQLITE_LIB  = $VBCC/sdk/SQLite3/lib
POSIXLIB_INC= $VBCC/sdk/PosixLib/include

ACE_HOME    = $VBCC/sdk/ACE
ACE_LIB     = $ACE_HOME/lib
ACE_INC     = $ACE_HOME/include
ACE_BMAPS   = $ACE_HOME/bmaps
```

Typischer Compile-/Link-Aufruf von Hand (ohne Makefile):

```sh
vc +aos68k -c99 -cpu=68000 -O2 \
   -I$VBCC_INC -I$NDK32_INC -I$NET_INC -I$AMISSL_INC \
   -c datei.c -o datei.o

vc +aos68k -cpu=68000 \
   -L$VBCC_LIB -L$NDK32_LIB -L$AMISSL_LIB \
   -o programm datei.o -lamiga
```
