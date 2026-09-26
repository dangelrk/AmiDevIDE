#ifndef AIDE_VERSION_H
#define AIDE_VERSION_H

/* Version and build stamp, shared by both frontends -- the Qt app via
 * qt/app/Version.h and the MUI app directly. It lives in core/ because
 * that is the one tree both of them already include; qt/ cannot be put on
 * the MUI app's include path, since qt/app/mainwindow.h would then shadow
 * amiga/src/app/mainwindow.h.
 *
 * Raise AMIGAIDE_VERSION_STRING by hand: the patch level for fixes, the
 * minor for a larger feature.
 *
 * The build number and date come from the build system as -D defines --
 * cmake/BuildStamp.cmake for the Qt build (via a generated header),
 * amiga/Makefile.amiga-cross for the MUI one. Both are the git commit
 * count with a trailing '+' when the working tree is dirty, and today's
 * date. Nothing is written into the source tree, so a rebuild can never
 * dirty the repository; outside a git checkout they degrade to "0" and
 * the compiler's own __DATE__. */

#define AMIGAIDE_VERSION_STRING "0.2.0"
#define AMIGAIDE_COPYRIGHT_STRING "Copyright \xC2\xA9 2026 Ronny Kr\xC3\xBCger"

#ifndef AMIGAIDE_BUILD_NUMBER
#define AMIGAIDE_BUILD_NUMBER "0"
#endif
#ifndef AMIGAIDE_BUILD_DATE
#define AMIGAIDE_BUILD_DATE __DATE__
#endif

#define AMIGAIDE_VERSION_FULL \
    AMIGAIDE_VERSION_STRING " build " AMIGAIDE_BUILD_NUMBER " (" AMIGAIDE_BUILD_DATE ")"

#endif /* AIDE_VERSION_H */
