#ifndef AIDE_VERSION_H
#define AIDE_VERSION_H

/* Version and build stamp, shared by both frontends -- the Qt app via
 * qt/app/Version.h and the MUI app directly. It lives in core/ because
 * that is the one tree both of them already include; qt/ cannot be put on
 * the MUI app's include path, since qt/app/mainwindow.h would then shadow
 * amiga/src/app/mainwindow.h.
 *
 * Raise AMIGAIDE_VERSION_STRING by hand with every release. Two numbers,
 * version.revision ("0.2", "1.0", "0.10"), nothing else: the Amiga's
 * Version command and the Installer's getversion compare exactly those
 * two, so "0.1.1" would look like "0.1" and an update would not be
 * recognised as newer.
 *
 * The build number and date come from a header the build system writes,
 * AmigaIDEBuildStamp.h -- cmake/BuildStamp.cmake for the Qt build,
 * amiga/Makefile.amiga-cross and amiga/Makefile.amiga for the MUI one. Not
 * -D defines: vc drops the quotes around a -D string value. The number is
 * the git commit count with a trailing '+' when the working tree is dirty,
 * the date today's, as dd.mm.yyyy. The header goes into the build tree,
 * never the source tree, so a rebuild can never dirty the repository.
 * Without git the number is "0"; a missing date falls back to the
 * compiler's own __DATE__. */

#define AMIGAIDE_VERSION_STRING "0.5"
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
