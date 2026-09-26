# AmiDevIDE

A complete AmigaOS/m68k cross-development environment that works the same on
macOS, Windows and Linux. Every tool is built from source, and no third-party
code is checked in: the bootstrap fetches it from the official sources.

**Installation, step by step for macOS, Linux, Windows and AmigaOS:
[`INSTALL.md`](INSTALL.md)** (German).

| | |
|---|---|
| **Compiler** | vbcc 0.9hP2 (C), ACE BASIC 3.0.1 + our patches (reports itself as 3.1.0) |
| **Assembler / linker** | vasm 2.0f, vlink 0.18a |
| **SDKs** | NDK 3.2, MUI 3.8, MCC, regex, reqtools, Roadshow, AmiSSL v5, SQLite3, PosixLib |
| **IDE** | Qt 6 on the host, MUI natively on the Amiga — one shared C99 core |
| **Testing** | FS-UAE, driven from the IDE or the command line |

## Getting started

```sh
cmake -B build
cmake --build build --target acd-toolchain     # fetch, build, install
cmake --build build --target acd-doctor        # check it
```

The first target goes from an empty checkout to a working m68k compiler: it
downloads the upstream sources, builds vbcc, vasm and vlink, installs the SDKs
and generates the configuration. It needs a C compiler, GNU make and Python
3.11+; on Windows that means MSYS2 with `mingw-w64-x86_64-gcc`.

Then:

```sh
source toolchain/setenv.sh                     # PowerShell: . toolchain\setenv.ps1
vc +aos68k -c99 -cpu=68000 -O2 hello.c -o hello
file hello                                     # AmigaOS loadseg()ble executable/binary
```

To build the IDE as well, point CMake at a Qt 6 installation:

```sh
cmake -B build -DCMAKE_PREFIX_PATH=$HOME/Qt/6.11.1/macos
cmake --build build
cd build && ctest
```

## Layout

```
bootstrap/       fetches sources, builds the tools, generates the configs
templates/       what the bootstrap instantiates: vc configs, setenv,
                 project Makefiles, the FS-UAE machine, VS Code settings
ide/             the IDE -- core/ (portable C99), qt/, amiga/ (MUI)
compilers/       ACE BASIC: the patch series against upstream 3.0.1
emulator/        FS-UAE runner
docs/            download list, the crosscompile guide

toolchain/       GENERATED -- this is what $VBCC points at
var/             GENERATED -- downloads, unpacked sources, emulator images
```

`toolchain/` and `var/` are gitignored. Nothing binary and nothing
third-party is checked in: the tools are built, the SDKs are unpacked from
their upstream archives, ACE BASIC is fetched and patched (see
`compilers/acebasic/README.md`), and the Kickstart ROM and Workbench image you
supply yourself (see `emulator/images/README.md`).

## Targets

| Target | What it does |
|---|---|
| `acd-toolchain` | everything below, in order |
| `acd-fetch` | download the upstream archives into `var/downloads/` |
| `acd-tools` | build vbcc, vasm, vlink |
| `acd-sdk` | unpack NDK 3.2 and MUI 3.8 |
| `acd-ace-src` | unpack upstream ACE BASIC and apply `compilers/acebasic/patches/` |
| `acd-ace` | build and install ACE BASIC |
| `acd-config` | generate the vc configs, `setenv.*`, the FS-UAE machine |
| `acd-seed` | copy the SDKs and ROMs that have no download from an existing tree |
| `acd-doctor` | check the installation and compile a test program |
| `acd-update-check` | report newer upstream releases, change nothing |
| `amigaide` | the Qt IDE |
| `amigaide-m68k` | the MUI IDE, cross-compiled (`-DACD_BUILD_IDE_AMIGA=ON`) |

## Options

| Option | Default | |
|---|---|---|
| `ACD_TOOLCHAIN_PREFIX` | `./toolchain` | the generated `$VBCC` root |
| `ACD_VAR_DIR` | `./var` | downloads, sources, emulator images |
| `ACD_BUILD_IDE_QT` | `ON` | auto-disabled when Qt 6 is not found |
| `ACD_BUILD_IDE_AMIGA` | `OFF` | needs a bootstrapped toolchain |
| `ACD_BUILD_TESTS` | `ON` | |
| `ACD_OFFLINE` | `OFF` | never touch the network |

## Working offline

`acd-fetch` uses whatever is already in `var/downloads/`, however it got there.
`docs/SOURCES.html` — generated from `bootstrap/sources.toml`, so its links
cannot go stale — lists every archive with its download URL. Drop them in and
configure with `-DACD_OFFLINE=ON`.

`.lha` needs an extractor no platform ships. The bootstrap tries `lha`,
`lhasa`, `7z`, `7zz` and finally the Python `lhafile` module, so
`pip3 install lhafile` is enough on a bare machine.

## Staying current

```sh
cmake --build build --target acd-update-check   # reports, changes nothing
python3 bootstrap/acd_fetch.py --update         # takes the new versions
```

vasm and vlink publish a stable "current release" URL; vbcc and its m68k target
archive are resolved by picking the newest entry off the vbcc download page.
Whatever is resolved is recorded in `bootstrap/sources.lock.json`, which is
checked in, so a build is reproducible even though the default is "latest".

## Testing in the emulator

```sh
python3 emulator/acd_emu.py --status
python3 emulator/acd_emu.py --binary hello     # copy into SHARED: and launch
python3 emulator/acd_emu.py --serial           # tail the program's output
```

The IDE's Run command does the same thing through the same logic. `SHARED:` is
a live directory mount, so when FS-UAE is already running the binary is simply
copied in and you restart it from the Amiga shell.

## Notes

**NDK 3.2 is newer than NDK 3.9.** The version numbers invert the usual
ordering. 3.9 has been dropped, along with MUI 5.0 — which only ever shipped
GCC `.a` libraries that vlink cannot link. Projects naming either are migrated
on load.

**vbcc's `dtgen` is interactive.** It asks nine questions about the host's
integer widths before vbcc can be compiled at all, which is why upstream ships
no build script. `bootstrap/acd_build_tools.py` answers them with the defaults
— correct for a 64-bit little-endian host targeting m68k — and then asserts
that the generated `dt.h` really does describe 32-bit `long` and 32-bit
pointers, because a wrong answer would mistype the whole compiler silently
rather than fail.

## Documentation

- `INSTALL.md` — installation on every platform, including AmigaOS
- `docs/CROSSCOMPILE.md` — the working guide: building, porting, testing
- `docs/SOURCES.html` — every external download, generated
- `compilers/acebasic/README.md` — the ACE patch series, and how to apply it
- `compilers/acebasic/UPDATING.md` — moving the patches to a new ACE release
- `AGENTS.md` — conventions and pitfalls

## License

AmiDevIDE is licensed under the GNU General Public License v3, see
`LICENSE`. Third-party components and their licenses are listed in
`THIRD_PARTY_NOTICES.md`.
