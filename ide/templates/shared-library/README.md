# AmigaOS shared library — working vbcc skeleton

Groundwork for a "Shared library" project type. **Not yet wired into the
IDE**: these two files build a valid `example.library` by hand, and are
kept here so the template does not have to be rediscovered.

## What it produces

```
vc +aos68k -c -cpu=68000 -O1 -I$NDK32_INC library.c -o library.o
vasmm68k_mot -Fhunk -quiet -o libstart.o libstart.s
vlink -bamigahunk -x -Bstatic -nostdlib -mrel -s \
      libstart.o library.o -L$VBCC_LIB -lvc -o example.library
```

484 bytes, verified: romtag `0x4AFC` present, library name and ID string
embedded, and the first code in the segment is `moveq #-1,d0 / rts` so
running it from the Shell fails cleanly instead of jumping into library
code. **Not yet run on real hardware or an emulator** — this checkout has
no bootable one.

## Why not SDI

The SDI headers' own `libraries/example_lib.c` is the obvious starting
point and `SDI_compiler.h` does support vbcc. But the example itself is
OS3/OS4/MorphOS-portable and uses `LIBPROTOVA`, whose varargs form vbcc
rejects ("declaration expected" at the `SPrintf` prototype). Rather than
carve up a 604-line multi-platform example, this skeleton uses vbcc's own
`__saveds` and `__reg("...")` annotations directly. It is OS3-only, about
100 lines, and has no dependency beyond the NDK.

## What the build system has to do differently

  - no startup object: the romtag is the entry point exec looks for
  - `-nostdlib`, and `libstart.s` must be the FIRST object on the link
    line so its `moveq #-1,d0 / rts` lands at offset 0 of the segment
  - the output name carries `.library`, not the bare project name

## What is the user's to maintain, not the generator's

The romtag, the init table and the function-vector table are source code:
adding a function to the library means adding its prototype, its
implementation *and* its slot in `LibVectors[]`, in that order — the table
order defines the LVOs, so inserting in the middle renumbers every call
site. A generator cannot infer that from the sources.
