# AmiDevIDE — conventions and pitfalls

Read this before changing anything. Most of it is hard-won: the traps below
were each found by something breaking, and several of them fail silently.

## Layout

| | |
|---|---|
| `bootstrap/` | Python: fetch, build the tools, generate the configs |
| `templates/` | what the bootstrap instantiates — never edit the generated copy |
| `ide/core/` | portable C99. No Qt, no MUI, no platform assumptions |
| `ide/qt/` | Qt 6 frontend. Thin adapters over `core/` |
| `ide/amiga/` | MUI frontend, built with vbcc for m68k |
| `compilers/acebasic/` | ACE BASIC **patch series** only — upstream is fetched |
| `emulator/` | FS-UAE runner |
| `toolchain/`, `var/` | **generated**, gitignored. Never commit anything here |

## The architecture, in one sentence

All logic lives headless in C99 under `ide/core/`; the Qt and MUI frontends
call the same `aide_*` functions and build their widgets natively. There is
deliberately **no GUI abstraction layer** between them — don't add one.

Platform differences go through backend pairs
(`aide_fs_posix.c` ↔ `aide_fs_amiga.c`, `aide_regex_posix.c` ↔
`aide_regex_amiga.c`) chosen by the build system, plus the `AideMakeDialect`
enum. Follow that pattern rather than inventing another.

## Things that will bite you

**There is one NDK and one MUI: NDK 3.2 and MUI 3.8.** Don't add another
version of either, not in the registry, the UI or the docs.
`tst_sdkregistry` asserts that the retired ids stay out of the registry, and
`acd-doctor` flags their directories if an old toolchain still has them.

**SDK selection is one list, and every entry is optional.** `sdks.selected`
holds registry ids in include-search order. An id the registry does not
know is skipped when the Makefile is generated. Not even the NDK is
mandatory: a program that drives the hardware directly needs no system
headers.

**Adding a file to `ide/core/src/` means editing `ide/core/sources.mk`,
and only that.** CMake and both Amiga Makefiles read it. This used to be
three separate lists and they had drifted: the native Amiga Makefile was
missing two ACE BASIC files for months.

**`(void)x;` does not work in code vbcc compiles.** It reports warning 153,
"statement has no effect". Leave the parameter unreferenced and say why in a
comment.

**vbcc's `dtgen` is an interactive questionnaire.** Nine questions about host
integer widths, asked before vbcc can be compiled at all. `acd_build_tools.py`
feeds it newlines to accept the defaults, then asserts the resulting `dt.h`
describes 32-bit `long` and 32-bit pointers — a wrong answer mistypes the
entire compiler silently instead of failing.

**vc needs `$VBCC` set.** It resolves `+aos68k` to `$VBCC/config/aos68k` and
execs `vbccm68k`, `vasmm68k_mot` and `vlink` by bare name through `PATH`.
Without it you get "No config file!", which reads like a broken binary.
`toolchain/setenv.sh` sets both; a Makefile that invokes vc must too.

**Never check in a config with a `~` in it.** vc execs its tools directly, not
through a shell, so `~` never expands. That is why `templates/vc-config/*.in`
exist and `acd_doctor.py` checks the generated output for stray `~` and `$`.

**No third-party code is checked in.** Not ACE, not vbcc/vasm/vlink, not
NDK/MUI headers, autodocs, libraries, manuals, ROMs or disk images. Anything
from outside goes through `bootstrap/sources.toml` and lands in the ignored
`var/` or `toolchain/`. The only exception is `ide/core/vendor/parson.*`
(MIT), listed in `THIRD_PARTY_NOTICES.md`.

**ACE BASIC is upstream 3.0.1 plus `compilers/acebasic/patches/`.**
`bootstrap/acd_ace.py` (target `acd-ace-src`) checks the archive's sha256,
unpacks it to `var/src/acebasic/`, commits it as tag `upstream` and applies
the series with `git am`. Change ACE there, commit, and export with
`git format-patch --no-signature --zero-commit -N upstream..` back into
`compilers/acebasic/patches/`. Never edit the patch files by hand, never
fork or submodule upstream. Moving to a new upstream release:
`compilers/acebasic/UPDATING.md`.

**The host runs the patched ACE, the Amiga ACE as shipped.** The Makefile
dialect decides, never the `.aproj`: the host dialect uses `yap -g`,
`ace -0` and `$(VBCC)/sdk/ACE`; the AmigaDOS dialect uses `ACE:bin`,
`ACElib:`, no `-g`, and for 68000 an `OPTION 2-` joined in front of the
preprocessed text. `tst_makefilegenerator` pins both.

**`LONG` is exactly 32 bits on the host and `long` under vbcc.** After any
change to ACE's `src/ace/c/`, run **both**:

```sh
cd var/src/acebasic/src/make
make -f Makefile-host
make -f Makefile-host amiga-check
```

Format conversions need explicit `(long)` casts with `%ld`. Only the
cross-build catches the mismatch.

**ACE keyword changes touch five files** in the ACE tree (so: a patch). The
four dictionaries under `IDE/CubicIDE-ACE/add-ons/ace/syntax/` plus
`docs/quickref.txt`. `ide/core/src/aide_basic_keywords.c` is generated from
them by `ide/core/tools/gen_basic_keywords.sh`, which reads
`var/src/acebasic/` by default.

**`ide/amiga/Makefile.amiga` uses explicit per-file rules on purpose.** No
pattern rules, no wildcards in prerequisites — see the reasoning in its
header. The rules are now emitted from `sources.mk` by `$(foreach)/$(eval)`,
which is plain variable expansion and unaffected by gnu_make_amiga.

**`CMAKE_SOURCE_DIR` inside `ide/` means the repository root**, not `ide/`,
because of the superbuild. Use `AMIGAIDE_ROOT`.

**QSettings outlives the tree it describes.** The IDE stores the toolchain
root, and moving or rebuilding the toolchain leaves it pointing at nothing --
the first sign of which is `make` reporting "No such file or directory" for
vc. `AppSettings::migrateStaleSettings()` repairs that at startup; extend it
whenever you add a setting that holds a path. `AppSettings::detectToolchainRoot()`
finds the tree from the executable's own location, so a changed default alone
never reaches an existing installation.

**On macOS, never deploy the bare `amigaide` executable.** macdeployqt rewrites
its rpath to `@executable_path/../Frameworks`, which only resolves inside the
bundle; copied to `toolchain/bin` it fails with "Library not loaded:
@rpath/QtWidgets.framework". `toolchain/bin/amigaide` is a launcher script
that execs the bundle.

## Before you commit

```sh
cmake --build build                 # Qt IDE
cd build && ctest                   # 17 suites, all must pass
cmake --build build --target acd-doctor
```

If you touched `ide/core/` or `ide/amiga/`, also:

```sh
cmake --build build --target amigaide-m68k
file ide/amiga/bin/amigaide         # AmigaOS loadseg()ble executable/binary
```

`tst_autodocindex` and `tst_sfdindex` read real NDK data and skip themselves
when the toolchain has not been bootstrapped. A skip is fine; a failure is not.

## Verified vs. not

macOS is fully verified, including a bootstrap from an empty tree. Linux and
Windows build everything from source and are structurally identical, but have
not been run end to end — `acd-doctor` is the way to find out.

`ide/amiga/Makefile.amiga` (native on-Amiga build) is verified only as far as
its compile rules, exercised on the host. The `makedir`/`delete` recipes, the
`vbcc:`/`SDK:` assigns and the final link need a real Amiga.
