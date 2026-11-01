# tools/release -- Amiga-Archiv und Aminet-Dateien

`amiga_release.py` baut aus der fertig übersetzten MUI-IDE die
Installationsschublade, das LhA-Archiv und die Aminet-Dateien
(CMake-Ziel `amiga-release`, siehe `install/amiga/`).

Die Hilfswerkzeuge stammen aus AmiCloudCore (`tools/`) und werden dort
gepflegt; hier liegen Kopien, damit AmiDevIDE ohne Submodul auskommt:

| Datei | Zweck |
|---|---|
| `lhapack.py` | LhA-Archiv (-lh5-) schreiben; macOS hat keinen LhA-Packer |
| `mkicon.py` | Workbench-Icons nach `install/amiga/icons.spec` (hier um das Bild `ide` ergänzt) |
| `check-guide.py` | AmigaGuide prüfen: Nodes, Links, Latin-1 |
| `aminet-readme-check.awk` | Aminet-Readme nach den Aminet-Regeln prüfen |
