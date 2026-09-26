#ifndef AIDE_REQTOOLS_STUBS_H
#define AIDE_REQTOOLS_STUBS_H

/*
 * reqtools_stubs.h -- vbcc-native inline LVO stubs for reqtools.library,
 * used for every requester in this app (string input, file open/save)
 * instead of a hand-rolled MUI input window -- see mainwindow.c's
 * ask_string()/ask_file(). Same problem and fix as mui_stubs.h/
 * aide_regex_amiga.c: reqtools.library's SDK only ships SAS/C
 * #pragma libcall headers, which vbcc silently ignores.
 *
 * LVO offsets taken directly from sdk/reqtools/fd/reqtools_lib.fd
 * (##bias 30, functions listed in order, -6 per entry) -- no guessing:
 * rtAllocRequestA -30, rtFreeRequest -36, rtChangeReqAttrA -48,
 * rtFileRequestA -54, rtGetStringA -72. Only the "A" (tag-array) entry
 * points are stubbed;
 * every call site here builds its TagItem array directly, so no
 * varargs-to-taglist wrapper (like mui_stubs.c's MUI_NewObject) is
 * needed.
 */

#include <libraries/reqtools.h>
#include <utility/tagitem.h>

extern struct Library *ReqToolsBase;

APTR __rtAllocRequestA(__reg("a6") void *, __reg("d0") ULONG type,
                        __reg("a0") struct TagItem *tags) = "\tjsr\t-30(a6)";
#define rtAllocRequestA(type, tags) __rtAllocRequestA(ReqToolsBase, (type), (tags))

void __rtFreeRequest(__reg("a6") void *, __reg("a1") APTR req) = "\tjsr\t-36(a6)";
#define rtFreeRequest(req) __rtFreeRequest(ReqToolsBase, (req))

LONG __rtChangeReqAttrA(__reg("a6") void *, __reg("a1") APTR req,
                         __reg("a0") struct TagItem *tags) = "\tjsr\t-48(a6)";
#define rtChangeReqAttrA(req, tags) __rtChangeReqAttrA(ReqToolsBase, (req), (tags))

LONG __rtFileRequestA(__reg("a6") void *, __reg("a1") APTR filereq, __reg("a2") STRPTR file,
                       __reg("a3") CONST_STRPTR title,
                       __reg("a0") struct TagItem *tags) = "\tjsr\t-54(a6)";
#define rtFileRequestA(filereq, file, title, tags) \
    __rtFileRequestA(ReqToolsBase, (filereq), (file), (title), (tags))

LONG __rtGetStringA(__reg("a6") void *, __reg("a1") STRPTR buffer, __reg("d0") LONG maxchars,
                     __reg("a2") CONST_STRPTR title, __reg("a3") APTR reqinfo,
                     __reg("a0") struct TagItem *tags) = "\tjsr\t-72(a6)";
#define rtGetStringA(buffer, maxchars, title, reqinfo, tags) \
    __rtGetStringA(ReqToolsBase, (buffer), (maxchars), (title), (reqinfo), (tags))

#endif /* AIDE_REQTOOLS_STUBS_H */
