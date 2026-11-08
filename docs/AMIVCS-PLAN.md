# Plan: AmiVCS in AmigaIDE (Qt und MUI)

Stand 02.11.2026, Fassung 4, umgesetzt (AmiVCS 4b50020, AmigaIDE a231226 ff.). Fassung 2 und 3 wurden von einem Subagenten
geprüft (Befunde eingearbeitet); umgesetzt sind die Entscheidungen des
Nutzers:

- Netzbefehle über die CLI `amivcs`: **ja**
- AmiVCS **nicht einkompilieren**, sondern als optionales Werkzeug
  einbinden, wie vc oder ACE: die IDE ruft das Programm `amivcs` auf
- Editor-Zustand aus der `.aproj`: **ja**, ohne Rücksicht auf alte Dateien
  (die IDE ist noch nicht im Einsatz)

## 1. Ziel

Versionsverwaltung in beiden Oberflächen gleich, wenn `amivcs` vorhanden
ist; ohne `amivcs` sieht die IDE aus wie heute.

- Repository für das Projekt anlegen oder erkennen
- Status: geänderte, neue, gelöschte Dateien; bereitstellen/zurücknehmen
- Commit mit Nachricht
- Verlauf, in Qt mit Branch-Graph
- Branches anlegen, wechseln, zusammenführen, mit Konfliktliste
- Server: Clone, Push, Pull
- Zustand der Dateien im Projektbaum

## 2. Grundsatz: `amivcs` ist ein Werkzeug wie `vc` und `ace`

- Die IDE linkt **nichts** aus AmiVCS. Jeder Vorgang, lokal wie übers
  Netz, ist ein Aufruf des Programms `amivcs` im Projektverzeichnis.
- Werkzeug-Einstellung wie bei Make und vbcc:
  - Qt: Einstellung „AmiVCS program“, Vorgabe `toolchain/bin/amivcs`,
    sonst Suche im PATH; immer als absoluter Pfad gespeichert (eine aus dem
    Finder gestartete App hat keinen Shell-PATH).
    `AppSettings::migrateStaleSettings()` erweitern.
  - MUI: `ENVARC:AmigaIDE/amivcs`, Vorgabe `amivcs` (C:).
  - Einmalige Prüfung beim Start und nach Änderung der Einstellung: `amivcs
    version`; fehlt das Programm oder ist es zu alt (§3), bleiben Menü und
    Ansichten ausgeblendet bzw. mit Hinweis gesperrt.
- **Folgen, die dieser Plan regelt:**
  - Die IDE braucht maschinenlesbare Ausgaben der CLI (§3).
  - Alles läuft asynchron über den vorhandenen Prozessweg der IDE (§4):
    die MUI-Oberfläche blockiert nie, auch nicht beim Status über viele
    Dateien.
  - Zeichensatz, Autor, Zugangsdaten bleiben ganz bei `amivcs`
    (seit AmiVCS 0.10: `ENV:AmiVCS/config` bzw. `~/.amivcsconfig`
    global, `.amivcs/config` pro Repository mit Remote-URL; früher
    `ENV:amivcs.cfg`, `~/.amivcsrc`): IDE und CLI können sich nicht
    widersprechen. Fehlt ein Login, meldet `amivcs` den Datensatz
    `auth-required <url> <benutzer>`; die IDE fragt und wiederholt den
    Auftrag mit `--user`/`--password`/`--remember-password` in der
    `--args-from`-Datei (siehe AmiVCS `docs/design/remotes-plan.md`). Auf dem Amiga sieht die IDE nur lokale Namen
    (Latin-1 usw.), die Umwandlung nach UTF-8 macht `amivcs`.
- Kein Submodul, keine gemeinsamen Quellen, keine Build-Änderungen an der
  IDE für AmiVCS. Der Bootstrap kann `amivcs` später (wenn AmiVCS
  öffentlich ist) wie die anderen Werkzeuge bauen und nach `toolchain/bin`
  legen; bis dahin trägt man den Pfad ein.

## 3. Was AmiVCS dafür bekommt (CLI, Version 0.6)

### 3.1 Fehler, die vorher behoben werden (betreffen auch amivcs-gui)

- **Zurücknehmen:** `amivcs remove` löscht den Pfad aus dem Index; bei
  einer versionierten Datei wird so aus „geändert, bereit“ ein bereit
  gestelltes Löschen. Neu: `amivcs unstage <pfad>` setzt den Eintrag auf
  den Stand von HEAD zurück (entfernt ihn nur, wenn HEAD ihn nicht hat).
  `vcsgui_remove` in libcloudvcsgui bekommt dasselbe.
- **Löschen bereitstellen:** `amivcs add` auf eine gelöschte Datei schlägt
  heute fehl; künftig stellt es das Löschen bereit.
- **Unversionierte Dateien zählen nicht als „schmutzig“:** `is_dirty`
  vergleicht HEAD mit dem Arbeitsbaum und zählt neue Dateien mit; jede
  noch nicht hinzugefügte Datei (auch `.theirs`) blockiert Switch, Merge,
  Checkout und Pull. Künftig blockieren nur geänderte und gelöschte
  versionierte Dateien; neue werden nur geblockt, wenn der Ziel-Stand
  eine Datei gleichen Namens schreiben würde.
- **Alle Ausgaben im lokalen Zeichensatz:** einige Meldungen geben
  Branch-Namen noch in Repository-Form (UTF-8) aus (repo.c, merge.c).
- **Abbrechen sicher machen:** auf dem Amiga `__chkabort()` leer
  definieren (sonst beendet vbccs stdio das Programm beim nächsten
  `printf` mitten im Schreiben) und Ctrl-C nur an sicheren Stellen
  prüfen; auf dem PC SIGINT/SIGTERM abfangen und `ui_break_set()` setzen;
  unter Windows Abbruch über stdin.
- **Sperrdatei** `.amivcs/lock` für schreibende Befehle, damit IDE, Shell
  und amivcs-gui nicht gleichzeitig schreiben.

### 3.2 Maschinenlesbare Ausgabe

- `--porcelain-out <datei>`: amivcs schreibt die Datensätze selbst in
  diese Datei und schließt sie vor dem Ende. Auf dem Amiga lassen sich
  stdout und stderr eines Kindprozesses nicht trennen (der Runner kennt
  nur `SYS_Output`), und Puffern spielt so keine Rolle.
- Ein Datensatz je Zeile, erstes Feld ist die Art, Felder durch Tab
  getrennt; in **jedem** Feld werden `\`, Tab, `\n`, `\r` maskiert (nicht
  abgewiesen: Dateinamen dürfen am PC Tabs enthalten). Alles im lokalen
  Zeichensatz; die Qt-IDE liest UTF-8 (`QString::fromUtf8`).
- Letzte Zeile immer `end<TAB><rückgabewert>` (ersetzt die
  `RC=$RC`-Endmarke des Execute-Skripts).

| Datensatz | Bedeutung |
|---|---|
| `format<TAB>1` | Formatversion; die IDE akzeptiert `>= 1` und überspringt unbekannte Datensätze |
| `branch<TAB>name` / `detached<TAB>oid` | aktueller Stand |
| `change<TAB>S\|U<TAB>N\|M\|D<TAB>pfad` | bereit (S) bzw. nicht bereit (U) |
| `ref<TAB>*\|-<TAB>name` | Branches, `*` = aktuell |
| `commit<TAB>oid<TAB>eltern(,)<TAB>mtime<TAB>autor<TAB>refs(,)<TAB>nachricht` | Verlauf; `mtime` ist UTC-Epoche, angezeigt als UTC |
| `msg<TAB>info\|warn\|error<TAB>text` | jede Meldung, also auch jeder Konflikt |
| `progress<TAB>fertig<TAB>gesamt<TAB>name` | Fortschritt bei clone/push/pull |
| `end<TAB>rc` | Ende |

- **Argumente aus Dateien statt Kommandozeile** (AmigaDOS deutet `;`,
  `$`, `*`, `<`, `>`, `"`, und die Zeilenlänge ist begrenzt):
  `--args-from <datei>` mit je einem Pfad, Branch-Namen oder
  Nachrichtentext pro Zeile (gleiche Maskierung); `commit -F` liest die
  Nachricht. Auf der Kommandozeile stehen nur der (gequotete)
  Programmpfad, der Befehl und feste `T:`-Dateinamen.
- **Ein Prozess statt zwei:** ändernde Befehle mit `--porcelain-out`
  (`add`, `unstage`, `commit`, `switch`, `merge`, `pull`) hängen den
  neuen Status (`branch`/`change`) an.
- `amivcs version --porcelain-out` liefert `format`; die IDE prüft nur
  die Formatversion, nicht die Programmversion. (AmiCloudCore wird dafür
  auf 0.6 gesetzt, damit `amivcs version` die neue Nummer zeigt.)
- Graph-Lanes rechnet die Qt-IDE aus den `commit`-Datensätzen;
  `graph.c` dient als Vorlage.
- Tests in AmiVCS: Golden-Dateien für alle Datensätze, Maskierung, Umlaute
  (lokal ↔ UTF-8), unstage/add-gelöscht, Sperrdatei, Abbruch.

## 4. Prozesse in der IDE

- **Zwei Bahnen:** lesende Aufträge (status, log, branch, version) dürfen
  neben Build/Run laufen; schreibende (add, unstage, commit, switch,
  merge, pull, clone, checkout, init) schließen Build/Run und andere
  schreibende Aufträge aus. Innerhalb jeder Bahn eine Warteschlange.
- **Qt:** ein `QProcess` je Auftrag, `setWorkingDirectory` =
  Projektwurzel, Argumente als Liste, Ergebnis aus der
  `--porcelain-out`-Datei; stdout/stderr nur fürs Ausgabe-Fenster.
- **MUI:** der Build-Runner (heute ein globaler Zustand mit festem
  `T:aide-build.out`) wird auf zwei Plätze mit eigenen Dateien und
  Zuständen erweitert. `amivcs` wird direkt per `SystemTagList` mit
  `NP_CurrentDir` gestartet, ohne Execute-Skript; das Ende erkennt die
  IDE an der `end`-Zeile der Ergebnisdatei (Logtail), Fortschritt an den
  `progress`-Zeilen.
- **Kern-Modul `aide_vcs`** (C99): Argumentdateien schreiben, Ergebnisse
  parsen, Meldungen sammeln, Dateizustände liefern. Es wandelt **keine**
  Namen um: verglichen werden lokale Pfade mit lokalen Pfaden, auf dem
  Amiga ohne Rücksicht auf Groß-/Kleinschreibung. Testbar ohne `amivcs`
  mit Golden-Dateien.

## 5. Projektdatei und Ignore

- **Editor-Zustand raus aus der `.aproj`:** `editor.openFiles` und
  `activeFile` wandern in `<projekt>.aproj.user` (Qt und MUI gleich,
  ignoriert). Keine Übergangslogik für alte Dateien.
- **`.amivcsignore`**: nach `amivcs init` und **bei jedem Öffnen eines
  Projekts mit Repository** (auch geklont oder in der Shell angelegt)
  prüft die IDE ihre Zeilen und bietet an, fehlende anzuhängen: `build/` (Makefile, vc.cfg,
  __stack.c, obj), das Zielverzeichnis (Vorgabe `bin/`), das Programm und
  sein `.info`, falls außerhalb davon, `*.aproj.user`. Keine allgemeine
  `.info`-Regel.
- Repository-Wurzel = Projektwurzel; liegt `.amivcs` nur weiter oben, sagt
  die IDE das, statt dort zu arbeiten.

## 6. Sicherheit der Daten (vor jeder Oberfläche, die Switch/Merge/Pull anbietet)

- **Vorher:** ungespeicherte Puffer und ein ungespeichertes Projekt →
  speichern oder abbrechen. Vor Pull außerdem `status --porcelain`: ist
  der Baum nicht sauber, gleich ablehnen statt erst herunterzuladen.
- **Nachher (Switch, Merge, Pull, Checkout, Clone):** Projekt neu laden,
  offene Dateien neu laden, gelöschte schließen, mit Hinweis.
- **Vor Switch/Merge/Pull** zeigt die IDE nicht versionierte Dateien,
  die im Weg wären, mit „committen, löschen oder ignorieren“, statt nur
  die Ablehnung der CLI weiterzureichen.
- **Sperren** (siehe §4): während ein schreibender Auftrag läuft, der den
  Arbeitsbaum ändern kann (switch, merge, pull, clone, checkout): kein
  Speichern (Qt: Editoren schreibgeschützt; MUI:
  `MUIA_TextEditor_ReadOnly` und Speichern-Menüpunkte gesperrt), kein
  Build/Run, keine weiteren schreibenden Aufträge. Lesende Aufträge
  dürfen neben Build/Run laufen.
- **Abbrechen** (setzt §3.1 voraus): Push und lesende Aufträge
  jederzeit; Pull/Clone brechen nur in der Download-Phase ab, danach
  läuft der Schreibteil zu Ende. Qt sendet SIGINT (Windows: stdin
  schließen), nie `kill()`; MUI vorerst ohne Abbruch, die Sperre hält
  bis zur `end`-Zeile.
- **Konflikte:** vollständige Liste aus den `msg warn`-Datensätzen im
  Ausgabe-Bereich; `.theirs`-Dateien erscheinen als neu; vor dem Commit
  Warnung, solange `.theirs`-Dateien da sind.

## 7. Refresh

`amivcs status` liest und hasht alle Dateien; auf dem 68020 dauert das.
Weil es asynchron läuft, blockiert es nichts, darf aber nicht ständig
laufen:

- Status nach VCS-Aufträgen, auf Knopfdruck und wenn die VCS-Ansicht
  sichtbar wird; nach dem Speichern höchstens alle paar Sekunden
  gebündelt (Qt) bzw. nur auf Knopfdruck (MUI, Einstellung).
- Log nur nach Commit/Switch/Merge/Pull; Verlauf auf 200 Einträge
  begrenzt (`-n`), mehr auf Anforderung.

## 8. Oberflächen

**Qt:** Dock „Version Control“ (Branch, Changes/Staged mit Mehrfachauswahl,
Stage/Unstage/Stage All, Commit-Feld, „all“), Dock „History“ mit Graph,
Menü „VCS“, Kennzeichen im Projektbaum, Einstellung des Programms.

**MUI:** linkes Register „Versionen“ (Branch, zwei NLists, Knöpfe,
einzeiliges Commit-Feld, „alle“), unteres Register „Verlauf“ (NList ohne
Graph), Menü „Versionen“, Eingaben über die reqtools-Requester,
Einstellung `AmiVCS-Programm` im Einstellungsfenster.

Der Projektbaum zeigt nur Dateien der `.aproj`; neue, nicht eingetragene
und gelöschte Dateien zeigt nur die VCS-Ansicht. Sie ist maßgeblich.
Projekteinträge mit `../` gelten als „nicht versioniert“.

**Clone:** eigener Dialog (Repository-Name, Zielverzeichnis), auch ohne
geöffnetes Projekt; danach die `.aproj` im Ziel öffnen (bei mehreren
auswählen lassen, bei keiner anbieten, eine anzulegen).

## 9. Reihenfolge

1. **AmiVCS 0.6:** Fehler aus §3.1 (unstage, gelöschte Dateien,
   is_dirty, lokale Ausgabe, sicheres Abbrechen, Sperrdatei), dann
   `--porcelain-out`, `--args-from`, `commit -F`, `format 1`; Tests.
2. **IDE-Kern:** `aide_vcs` (Kommandozeilen, Parser, Meldungen,
   Ignore-Zeilen), `.aproj.user`, `tst_vcs` mit Golden-Ausgaben.
3. **Prozesse und Schutz:** zwei Bahnen mit Warteschlangen, MUI-Runner
   mit zwei Plätzen und direktem Start, Sperren, Speichern vorher /
   Neuladen nachher, Ignore-Prüfung beim Öffnen.
4. Qt-Oberfläche.
5. MUI-Oberfläche.
6. Netzbefehle und Clone-Dialog.
7. Doku (Guide-Kapitel „Versionsverwaltung“, INSTALL.md, AGENTS.md),
   Emulatortest.

## 10. Tests

- AmiVCS: Datensätze als Golden-Dateien, Maskierung (Tab, Zeilenumbruch,
  Backslash), Umlaute (lokal ↔ UTF-8), unstage einer versionierten Datei,
  Löschen bereitstellen, Switch mit unversionierten Dateien, Sperrdatei,
  Abbruch in Download- und Schreibphase, `make server-test`.
- IDE `tst_vcs`: Parser gegen Golden-Dateien (Konflikt-Meldungen,
  maskierte Felder, unbekannte Datensätze, fehlende `end`-Zeile),
  Argumentdateien, Ignore-Zeilen gegen echte makegen-Ausgabe, `.aproj` ohne
  Editor-Zustand; optional ein Durchlauf gegen ein echtes `amivcs` aus
  `toolchain/bin`, übersprungen wenn es fehlt.
- Emulator: Register bedienen, Commit mit Umlauten, Switch mit offener
  geänderter Datei, Konflikt-Merge, Push/Pull gegen einen Testserver
  (nicht gegen die Produktion schreiben ohne Freigabe), IDE ohne `amivcs`.

## 11. Später

Diff-Ansicht (`amivcs diff --porcelain` oder eigenes Diff-Modul),
Konfliktauflösung im Editor, Repository oberhalb der Projektwurzel
(braucht Suche nach oben in AmiVCS), Bootstrap baut `amivcs` mit, Abbruch
auf dem Amiga per `Signal(SIGBREAKF_CTRL_C)` an einen benannten Prozess
(sicher erst mit §3.1).
