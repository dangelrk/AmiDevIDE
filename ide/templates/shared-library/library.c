/* Minimal AmigaOS 3 shared library, vbcc-native (no SDI). */
#include <exec/types.h>
#include <exec/libraries.h>
#include <exec/resident.h>
#include <exec/execbase.h>
#include <dos/dos.h>
#include <proto/exec.h>

#define LIBNAME    "example.library"
#define LIBVERSION 1
#define LIBREVISION 0

struct ExecBase *SysBase;

struct MyLibrary {
    struct Library lib;
    BPTR           segList;
};

/* forward */
__saveds struct MyLibrary *LibInit(__reg("d0") struct MyLibrary *base,
                                   __reg("a0") BPTR seglist,
                                   __reg("a6") struct ExecBase *sysbase);
__saveds struct MyLibrary *LibOpen(__reg("a6") struct MyLibrary *base);
__saveds BPTR LibClose(__reg("a6") struct MyLibrary *base);
__saveds BPTR LibExpunge(__reg("a6") struct MyLibrary *base);
__saveds LONG LibReserved(void);
__saveds LONG Triple(__reg("d0") LONG n, __reg("a6") struct MyLibrary *base);

static const char LibName[] = LIBNAME;
static const char LibIdString[] = LIBNAME " 1.0 (11.08.2026)";

static const APTR LibVectors[] = {
    (APTR)LibOpen,
    (APTR)LibClose,
    (APTR)LibExpunge,
    (APTR)LibReserved,
    (APTR)Triple,
    (APTR)-1
};

static const ULONG LibInitTab[] = {
    sizeof(struct MyLibrary),
    (ULONG)LibVectors,
    0,
    (ULONG)LibInit
};

/* The romtag. Exec scans for this after LoadSeg(). */
const struct Resident ROMTag = {
    RTC_MATCHWORD, (struct Resident *)&ROMTag, (APTR)(&ROMTag + 1),
    RTF_AUTOINIT, LIBVERSION, NT_LIBRARY, 0,
    (char *)LibName, (char *)LibIdString, (APTR)LibInitTab
};

__saveds struct MyLibrary *LibInit(__reg("d0") struct MyLibrary *base,
                                   __reg("a0") BPTR seglist,
                                   __reg("a6") struct ExecBase *sysbase)
{
    SysBase = sysbase;
    base->segList = seglist;
    return base;
}

__saveds struct MyLibrary *LibOpen(__reg("a6") struct MyLibrary *base)
{
    base->lib.lib_OpenCnt++;
    base->lib.lib_Flags &= ~LIBF_DELEXP;
    return base;
}

__saveds BPTR LibClose(__reg("a6") struct MyLibrary *base)
{
    if (--base->lib.lib_OpenCnt == 0 && (base->lib.lib_Flags & LIBF_DELEXP))
        return LibExpunge(base);
    return 0;
}

__saveds BPTR LibExpunge(__reg("a6") struct MyLibrary *base)
{
    BPTR seg;

    if (base->lib.lib_OpenCnt) {
        base->lib.lib_Flags |= LIBF_DELEXP;
        return 0;
    }
    seg = base->segList;
    Remove((struct Node *)base);
    FreeMem((char *)base - base->lib.lib_NegSize,
            base->lib.lib_NegSize + base->lib.lib_PosSize);
    return seg;
}

__saveds LONG LibReserved(void) { return 0; }

/* The one real entry point this example offers. */
__saveds LONG Triple(__reg("d0") LONG n, __reg("a6") struct MyLibrary *base)
{
    return n * 3;
}
