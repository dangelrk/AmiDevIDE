# ACE BASIC — Patch-Satz

ACE BASIC ist **nicht** Teil dieses Repositorys. Hier liegen nur unsere
Änderungen, als Patch-Serie gegen genau ein Upstream-Release:

| | |
|---|---|
| Upstream | <https://github.com/mdbergmann/ACEBasic> (Maintainer: Manfred Bergmann; ursprünglicher Autor: David Benn) |
| Basis | Tag **`3.0.1`** — Archiv `https://github.com/mdbergmann/ACEBasic/archive/refs/tags/3.0.1.tar.gz` |
| SHA-256 | `8a38419e35c31cf7474385384940166c4e97f2fda36c46501e5d7d40900a556f` (steht auch in `bootstrap/sources.lock.json`) |
| Lizenz | GPL (ACE), die Patches stehen unter derselben Lizenz |
| Patches | `patches/0001-*.patch` … `patches/0012-*.patch`, Format `git format-patch` |

Das Original wird nicht verändert und nicht geforkt. Der Bootstrap lädt das
Archiv, prüft die Prüfsumme und spielt die Patches ein.

## Was die Patches ändern

| Patch | Inhalt |
|---|---|
| 0001 | `.gitignore` für Host-Build-Artefakte, Testausgaben und Fehlerlogs |
| 0002 | `ace` als Host-Cross-Compiler baubar: `hostcompat.h` (Amiga-Typen mit festen Breiten), `malloc`-Liste statt `AllocRemember()`, Ctrl-C über `SIGINT` (`break.c`), `Makefile-host` |
| 0003 | `make -f Makefile-host amiga-check`: baut `ace` vom Host aus für m68k |
| 0004 | `yap` (Präprozessor, im Original in ACE BASIC geschrieben) als C-Port für den Host |
| 0005 | `bas` (im Original ein AmigaDOS/ARexx-Skript) als C-Programm für den Host |
| 0006 | Golden-File-Vergleich: Host-`ace` gegen m68k-`ace`, Byte für Byte |
| 0007 | Installation in die vbcc-Toolchain (`$VBCC/bin`, `$VBCC/sdk/ACE`, `$VBCC/config/ace68k`) |
| 0008 | `yap -g` schreibt `#line`-Marker, `ace` meldet Fehler mit Originaldatei und Originalzeile |
| 0009 | Fix: Use-after-free im Peephole-Optimierer (`-O` erzeugte kaputten Assembler); `.bmap`-Suche ohne Assign |
| 0010 | `docs/upstream-findings.md`: die fünf gefundenen Compiler-Fehler, beschrieben für Upstream |
| 0011 | Versionsstempel (Version, Build-Nummer, Datum) in allen Binaries; Version 3.1.0 |
| 0012 | `yap -V` antwortet vor der Argumentprüfung |

Details stehen in den Commit-Texten der Patches selbst und in
`docs/upstream-findings.md` des gepatchten Baums.

## Automatisch (normaler Weg)

```sh
cmake --build build --target acd-ace        # holt, patcht, baut, installiert
```

Das sind drei Schritte:

1. `acd-fetch` lädt `ACEBasic-3.0.1.tar.gz` nach `var/downloads/`.
2. `acd-ace-src` (`bootstrap/acd_ace.py`) prüft die SHA-256, entpackt nach
   `var/src/acebasic/`, legt dort ein Git-Repository an (Commit + Tag
   `upstream` = unverändertes Original) und wendet die Patches mit `git am`
   an. Ist der Baum schon aktuell, passiert nichts.
3. `acd-ace` baut mit `make -f Makefile-host` und installiert nach
   `toolchain/`.

`python3 bootstrap/acd_ace.py --force` verwirft `var/src/acebasic/` und baut
den Baum neu auf.

## Von Hand

Voraussetzung: `git`, `curl`, `tar`, ein C-Compiler, GNU make und eine
gebootstrappte Toolchain (für `install`).

```sh
curl -L -o ACEBasic-3.0.1.tar.gz \
     https://github.com/mdbergmann/ACEBasic/archive/refs/tags/3.0.1.tar.gz
shasum -a 256 ACEBasic-3.0.1.tar.gz    # muss 8a38419e…a556f ergeben (Linux: sha256sum)
tar xzf ACEBasic-3.0.1.tar.gz
cd ACEBasic-3.0.1
git init -q && git add -A && git commit -qm "Upstream 3.0.1" && git tag upstream
git am /pfad/zu/AmiDevIDE/compilers/acebasic/patches/*.patch
cd src/make
make -f Makefile-host
make -f Makefile-host install VBCC=/pfad/zu/AmiDevIDE/toolchain
```

`git am` statt `patch -p1`, weil die Serie ausführbare Skripte anlegt
(Dateimodus) und die Commit-Texte erhalten bleiben sollen.

## Am Patch-Satz arbeiten

Den Arbeitsbaum `var/src/acebasic/` wie ein normales Repository behandeln:

```sh
cd var/src/acebasic
# ändern, testen, committen ...
git format-patch --no-signature --zero-commit -N -o ../../../compilers/acebasic/patches upstream..
```

Vorher die alten Dateien in `compilers/acebasic/patches/` löschen, damit keine
verwaisten Patches liegen bleiben. Solange `var/src/acebasic/` uncommittete
Änderungen hat, überschreibt `acd_ace.py` den Baum nicht.

Nach jeder Änderung an `src/ace/c/` **beide** Builds prüfen:

```sh
cd var/src/acebasic/src/make
make -f Makefile-host
make -f Makefile-host amiga-check
```

Für ein neues Upstream-Release siehe [`UPDATING.md`](UPDATING.md).

## Bekannte Probleme

- **`make -f Makefile-host amiga-check` bricht in `ver.c` ab** („illegal
  number“). Seit Patch 0011 werden `ACE_BUILD_NUMBER`/`ACE_BUILD_DATE` als
  `-D…='"…"'` übergeben. `vc` reicht die Optionen über eine Shell an
  `vbccm68k` weiter, die dabei die inneren Anführungszeichen entfernt. Der
  Host-Build ist nicht betroffen.
- **Nativer Amiga-Einsatz mit der IDE:** Die von der IDE erzeugten
  BASIC-Makefiles rufen `yap -g` und `ace -0`/`-2` auf. Beides gibt es nur
  mit diesen Patches. Das `ace`/`yap` aus dem Upstream-Archiv bzw. von Aminet
  kennt diese Optionen nicht. Ein m68k-`ace` entsteht über `amiga-check`
  (siehe oben). `yap.c` lässt sich mit `vc +aos68k` übersetzen, `bas.c` nicht
  (braucht `unistd.h`). Ein Build-Ziel dafür gibt es noch nicht.
