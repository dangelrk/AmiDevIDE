# ACE-Patches auf ein neues Upstream-Release portieren

Anleitung für Claude Code (und Menschen). Ziel: Die Patch-Serie in
`compilers/acebasic/patches/` so anpassen, dass sie auf ein neues Release von
<https://github.com/mdbergmann/ACEBasic> passt. Das Original wird dabei
**nie** verändert, **nicht** geforkt und **nicht** als Submodul eingebunden.

## Regeln

- Nur `compilers/acebasic/patches/`, `bootstrap/sources.toml`,
  `bootstrap/sources.lock.json` und die Doku in diesem Repository ändern. Nie
  etwas von Upstream ins Repository kopieren.
- Jeder Patch behält seinen Zweck und seinen Commit-Text. Konflikte löst man
  im Sinne des Patches; wenn Upstream das Problem inzwischen selbst behoben
  hat, fällt der Patch (oder der betroffene Teil) weg. Das in der
  Zusammenfassung ausdrücklich nennen.
- Keine neuen Funktionen nebenbei. Wenn beim Portieren ein echter Fehler
  auffällt, als eigenen, neuen Patch am Ende der Serie anlegen.
- Autor der Patches bleibt, wie er ist: `git am` und `git rebase` erhalten
  ihn. Nicht mit `--reset-author` arbeiten.
- Vor dem Fertigmelden müssen alle Prüfungen unten laufen. Was nicht
  geprüft werden konnte, wird als ungeprüft gemeldet.

## Ablauf

Die Beispiele nehmen an, dass das neue Tag `3.1.0` heißt. Arbeitsverzeichnis
ist das Scratchpad bzw. ein temporäres Verzeichnis, **nicht** `var/src/`.

### 1. Alten Stand herstellen

```sh
cmake --build build --target acd-ace-src     # var/src/acebasic = 3.0.1 + Patches
```

### 2. Beide Releases in ein Repository holen

```sh
git clone https://github.com/mdbergmann/ACEBasic.git ace-port
cd ace-port
git checkout -b port 3.0.1
git am ../AmiDevIDE/compilers/acebasic/patches/*.patch   # muss sauber durchlaufen
git log --oneline 3.0.1..                                    # die Serie als Commits
```

Wichtig: Die Patches sind gegen das **GitHub-Tag-Archiv** erzeugt. Der Baum
von Tag `3.0.1` im Git-Repository ist inhaltsgleich. Läuft `git am` hier schon
nicht durch, erst prüfen, ob das Tag verschoben wurde (Archiv neu laden und
die SHA-256 mit `bootstrap/sources.lock.json` vergleichen).

### 3. Auf das neue Tag umsetzen

```sh
git log --oneline 3.0.1..3.1.0               # was hat Upstream geändert?
git diff --stat 3.0.1 3.1.0
git rebase --onto 3.1.0 3.0.1 port
```

Bei Konflikten pro Commit:

- `git status` bzw. `git diff`: Upstream-Änderung und Patch-Absicht lesen.
- Den Commit-Text des Patches lesen; er beschreibt, *warum* er existiert.
- Auflösen, `git add`, `git rebase --continue`.
- Ist der Patch überflüssig geworden (z. B. ein Bugfix aus
  `docs/upstream-findings.md` ist upstream angekommen): `git rebase --skip`
  und notieren.

Patches, die sich besonders oft reiben werden:

- **0002** (`alloc.c`, `lex.c`, `acedef.h`): tiefe Eingriffe in den Compiler.
- **0011** (`ver.c`, `VERSION`): Versionsnummer. Beim Portieren die eigene
  Version sinnvoll über die neue Upstream-Version setzen.
- **0010** (`docs/upstream-findings.md`): Fehler, die upstream behoben
  sind, aus dem Dokument streichen.

### 4. Prüfen

Im portierten Baum (Toolchain aus diesem Repository verwenden):

```sh
export VBCC=/pfad/zu/AmiDevIDE/toolchain
cd src/make
make -f Makefile-host clean all              # Host-Build: ace, yap, bas
make -f Makefile-host amiga-check            # m68k-Build von ace (siehe Bekannte Probleme in README.md)
cd ../../utils/yap/tests && ./run_tests.sh   # yap gegen das BASIC-Original
```

Wenn die Testfälle vorhanden sind, zusätzlich
`verify/scripts/compare-host-vs-amiga.sh` (Host-`ace` gegen m68k-`ace`).

### 5. Serie exportieren

```sh
rm ../AmiDevIDE/compilers/acebasic/patches/*.patch
git format-patch --no-signature --zero-commit -N \
    -o ../AmiDevIDE/compilers/acebasic/patches 3.1.0..port
```

### 6. Pin im Bootstrap umstellen

Das Archiv laden und die Prüfsumme bestimmen:

```sh
curl -L -o ACEBasic-3.1.0.tar.gz \
     https://github.com/mdbergmann/ACEBasic/archive/refs/tags/3.1.0.tar.gz
shasum -a 256 ACEBasic-3.1.0.tar.gz
```

- `bootstrap/sources.toml`, Eintrag `acebasic`: `url`, `filename`, `patterns`.
- `bootstrap/sources.lock.json`, Eintrag `acebasic`: `url`, `filename`,
  `version`, `sha256`, `resolved` (heutiges Datum).
- `compilers/acebasic/README.md`: Basis-Tag, SHA-256, Patch-Tabelle.
- `INSTALL.md` und `README.md` (Versionsangaben).

### 7. Ende-zu-Ende im Repository

```sh
python3 bootstrap/acd_fetch.py               # lädt das neue Archiv
python3 bootstrap/acd_ace.py --force         # entpacken, prüfen, patchen
cmake --build build --target acd-ace
cmake --build build --target acd-doctor
sh ide/core/tools/gen_basic_keywords.sh      # Schlüsselwörter für die IDE
git diff --stat ide/core/src/aide_basic_keywords.c
cmake --build build && (cd build && ctest)
```

Hat sich `aide_basic_keywords.c` geändert, hat Upstream Schlüsselwörter
hinzugefügt oder entfernt. Die Änderung gehört in denselben Commit.

## Zusammenfassung am Ende

Kurz berichten: neues Tag und SHA-256, welche Patches unverändert, angepasst
oder entfallen sind (mit Grund), welche Prüfungen gelaufen sind und welche
nicht.
