#ifndef AIDE_VCCFG_H
#define AIDE_VCCFG_H

/* Portable C99 mirror of build/VcConfigGenerator.h -- writes
 * <project>/build/vc.cfg: compile-only vc profile (-cc/-as/-ul/-cf
 * -ml=1000 -hunkdebug -amiga-softfloat, +-sd for smallData), WITHOUT the
 * -ld/-l2 linker directives, since linking is done directly with vlink
 * from the generated Makefile. */

#include "aide/aide_makegen.h" /* AideMakeDialect */
#include "aide/aide_project.h"
#include "aide/aide_toolchain.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Picks -rm=/-rmv= via aide_makegen_host_dialect() (vbcc's __M68K__
 * macro) -- a thin wrapper around aide_vccfg_generate_dialect() kept
 * for the Qt call site. Returns a freshly malloc'd vc.cfg text; caller
 * frees(). */
char *aide_vccfg_generate(const AideToolchainConfig *toolchain, const AideProject *project);

/* Same as aide_vccfg_generate(), with an explicit dialect -- use this
 * directly from a call site that already KNOWS which host vc will run
 * on regardless of what __M68K__ says about the machine core/ happens
 * to be compiled on (the native Amiga app: it only ever runs on
 * AmigaOS, so there is no reason to infer that from a macro at all).
 * AIDE_MAKE_DIALECT_AMIGADOS -> "-rm=delete %s"; POSIX -> "-rm=rm -f %s". */
char *aide_vccfg_generate_dialect(const AideToolchainConfig *toolchain, const AideProject *project,
                                   AideMakeDialect dialect);

#ifdef __cplusplus
}
#endif

#endif /* AIDE_VCCFG_H */
