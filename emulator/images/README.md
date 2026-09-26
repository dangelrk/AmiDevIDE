# Emulator images

FS-UAE needs a Kickstart ROM and a Workbench hard disk image. Neither is in
this repository and neither can be: both are licensed Amiga system software.
You supply them yourself, and the bootstrap picks them up.

## Where they go

```
var/emu/kickstarts/amiga-os-310-a1200.rom     Kickstart 3.1 (A1200)
var/emu/hdf/workbench-311.hdf                 Workbench 3.1 hard disk image
var/emu/adf/*.adf                             floppy images (optional)
```

Then regenerate the machine description so it points at them:

```sh
cmake --build build --target acd-config
cmake --build build --target acd-doctor       # confirms both were found
```

## Getting them

**Legally**: Cloanto's *Amiga Forever* ships Kickstart ROMs and Workbench with
a licence to use them. It is the only supported route.

**From your own hardware**: if you own the machine, you may dump its ROM.

If you already have FS-UAE set up, the bootstrap looks in `~/FS-UAE/Kickstarts`
and `~/FS-UAE/Hard Drives` as well, so an existing installation is found
without copying anything:

```sh
python3 bootstrap/acd_seed_from_legacy.py
```

That also takes them from an older `~/Dev/amiga` tree, along with the SDKs that
have no download (MCC, regex, reqtools, Roadshow, AmiSSL, SQLite3, PosixLib).

## Overriding the paths

```sh
python3 bootstrap/acd_gen_config.py \
    --kickstart ~/roms/kick31.rom \
    --workbench ~/images/wb311.hdf
```

## What the generated machine looks like

A1200, Kickstart 3.1, 2 MB Chip + 8 MB Fast RAM, and two drives:

| | |
|---|---|
| `DH0` | the Workbench image |
| `SHARED` | `toolchain/shared`, a live host directory |

`SHARED:` is where the IDE and `acd_emu.py` copy freshly built binaries. Being
a directory mount rather than an image, a running emulator sees a new build
immediately — no restart, just run it again from the Amiga shell.

The machine also sets `serial_port` to `toolchain/shared/serial.log`, so
anything a program writes to the serial port lands in a file on the host.
`python3 emulator/acd_emu.py --serial` tails it; the IDE shows it in the Output
pane.

Before this was generated there were three hand-maintained configs that had
drifted apart — the Windows one mounted the shared folder as `DH0` and left the
Workbench image out altogether. There is one template now, and the paths that
differ per machine are filled in at bootstrap time.
