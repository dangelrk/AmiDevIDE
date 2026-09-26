#ifndef AIDE_MAKEGEN_H
#define AIDE_MAKEGEN_H

/* Portable C99 mirror of build/MakefileGenerator.h -- generates a
 * complete, self-standing GNU Makefile for a project. Golden-file tested against
 * tests/golden/(name).makefile via the Qt adapter in
 * src/build/MakefileGenerator.cpp, which is the trust anchor for this
 * port: it must keep reproducing those files byte-for-byte. */

#include "aide/aide_project.h"
#include "aide/aide_sdk.h"
#include "aide/aide_toolchain.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Which command dialect the generated Makefile's recipes use. This is a
 * property of whatever machine will run `make` against the file, not of
 * who *generated* it -- Qt cross-compiles from macOS/Linux/Windows (a
 * real POSIX `make`+shell), the native MUI app runs `make` directly on
 * AmigaOS (gnu_make_amiga, no Unix shell underneath -- see
 * README.Amiga: "Runs Amiga-Commands with SystemTags() (Execute)").
 * Same distinction aide_vccfg.c's __M68K__ branch already makes for
 * vc.cfg's -rm=/-rmv= directives. */
typedef enum AideMakeDialect {
    AIDE_MAKE_DIALECT_POSIX = 0,
    AIDE_MAKE_DIALECT_AMIGADOS = 1
} AideMakeDialect;

/* AMIGADOS when core/ itself is compiled for m68k-amigaos (vbcc's own
 * __M68K__ macro), POSIX otherwise -- mirrors aide_vccfg.c:61 exactly. */
AideMakeDialect aide_makegen_host_dialect(void);

int aide_makegen_needs_stack_object(const AideProject *project);
/* Freshly malloc'd "#include <stddef.h>\nsize_t __stack = N;\n". */
char *aide_makegen_stack_object_source(int stack_size);
/* Freshly malloc'd absolute path to the built binary. Dialect-agnostic:
 * always POSIX-joined against `project_root`, since this is used for our
 * own file I/O (Open()/fopen() on an absolute path), never placed on
 * vc's/vasm's/vlink's command line -- unrelated to the dialect question. */
char *aide_makegen_target_binary_path(const AideProject *project, const char *project_root);

/* `sdk_registry` is searched (by id) for every SDK referenced by
 * project->sdks (ndk, mui if not "none", extra[]) -- pass every known
 * descriptor (built-ins + user-defined), mirroring SdkRegistry::instance().
 * `project_root` is the absolute directory containing the .aproj file
 * (Project::projectRoot() on the Qt side) -- POSIX dialect only; ignored
 * for AMIGADOS (see aide_makegen_generate_dialect()). Returns a freshly
 * malloc'd Makefile text; caller frees(). Always generates the POSIX
 * dialect -- a thin wrapper around aide_makegen_generate_dialect() kept
 * for every existing (Qt) caller; use the dialect entry point directly
 * to generate (or test) the other dialect. */
char *aide_makegen_generate(const AideToolchainConfig *toolchain, const AideProject *project,
                             const char *project_root, const AideSdkList *sdk_registry);

/* Same as aide_makegen_generate(), with an explicit dialect.
 *
 * For AIDE_MAKE_DIALECT_AMIGADOS, `project_root` is NOT joined into any
 * path -- OBJDIR/BINDIR/TARGET and every source/header/vc.cfg path are
 * emitted exactly as stored on the project (already relative to the
 * project root, see aide_project.h), on the assumption that whoever
 * *runs* the resulting Makefile does so with the process's current
 * directory set to the project root (native Amiga side: via
 * SystemTagList()'s NP_CurrentDir tag). That is what lets the Amiga
 * build avoid ever putting an absolute, possibly space-containing
 * project path on vc's/vasm's/vlink's command line (vc's own frontend
 * re-splits its command line on whitespace internally, so quoting alone
 * can't fix a space in an absolute path -- see
 * amiga/src/app/mainwindow.c's former AIDEBUILD: assign, now removed).
 * SDK/toolchain paths (whatever `toolchain->root` and the SDK registry's
 * rootDirs resolve to) stay absolute in both dialects -- they live
 * outside the project and are expected to be space-free by convention
 * ("SDK:", "vbcc:"). */
char *aide_makegen_generate_dialect(const AideToolchainConfig *toolchain, const AideProject *project,
                                     const char *project_root, const AideSdkList *sdk_registry,
                                     AideMakeDialect dialect);

#ifdef __cplusplus
}
#endif

#endif /* AIDE_MAKEGEN_H */
