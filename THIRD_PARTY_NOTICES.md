# Third-party notices

AmiDevIDE itself is licensed under the GNU General Public License v3
(see `LICENSE`). This file lists what in or around the repository comes from
someone else.

## Included in this repository

| Component | Where | License |
|---|---|---|
| Parson 1.1.0, © 2012–2020 Krzysztof Gabis | `ide/core/vendor/parson.{c,h}` | MIT (header preserved in the files); locale-independent number parsing/printing added, marked "AmigaIDE:" |

`ide/core/src/aide_basic_keywords.c` is generated from the keyword
dictionaries shipped with ACE BASIC (`IDE/CubicIDE-ACE/add-ons/ace/syntax/`).
It contains only the names of the language's keywords and functions.

`compilers/acebasic/patches/` holds our own modifications to ACE BASIC,
distributed as patches against the unmodified upstream release under ACE's
license (GPL).

## Downloaded by the bootstrap, never checked in

These are fetched from their official locations into `var/` (ignored by git)
and built or unpacked into `toolchain/` (also ignored). Their licenses apply
to them; see each archive for the exact terms. The URLs are listed in
`bootstrap/sources.toml`.

| Component | Authors | Source | License, in short |
|---|---|---|---|
| vbcc | Volker Barthelmann | <http://sun.hasenbraten.de/vbcc/> | free for non-commercial use; redistribution only unmodified |
| vasm | Volker Barthelmann, Frank Wille | <http://sun.hasenbraten.de/vasm/> | same terms as vbcc |
| vlink | Frank Wille | <http://sun.hasenbraten.de/vlink/> | same terms as vbcc |
| vbcc target m68k-amigaos | Volker Barthelmann, Frank Wille | <http://sun.hasenbraten.de/vbcc/> | same terms as vbcc |
| NDK 3.2 | Hyperion Entertainment | <https://aminet.net/package/dev/misc/NDK3.2> | see the NDK's own license |
| MUI 3.8 developer files | Stefan Stuntz | <https://aminet.net/package/dev/mui/mui38dev> | see the archive |
| ACE BASIC | David Benn; maintained by Manfred Bergmann | <https://github.com/mdbergmann/ACEBasic> | GPL |

## Supplied by the user, never checked in

Kickstart ROMs, Workbench disks and hard-disk images, and the SDKs without a
fixed download (MCC, regex, reqtools, Roadshow, AmiSSL, SQLite3, PosixLib).
See `INSTALL.md`.
