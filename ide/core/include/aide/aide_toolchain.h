#ifndef AIDE_TOOLCHAIN_H
#define AIDE_TOOLCHAIN_H

/* Portable C99 mirror of build/ToolchainConfig.h -- resolves the fixed,
 * per-OS toolchain paths under a toolchain root (verified ground truth:
 * bin/{vc,vasmm68k_mot,vlink}, targets/m68k-amigaos). */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AideToolchainConfig {
    char *root;
} AideToolchainConfig;

void aide_toolchain_init(AideToolchainConfig *tc, const char *root);
void aide_toolchain_free(AideToolchainConfig *tc);

/* ".exe" when built for Windows, "" otherwise -- decided at compile time
 * via #ifdef _WIN32/__CYGWIN__, matching Q_OS_WIN's scope exactly. */
const char *aide_toolchain_exe_suffix(void);

/* All of the following return a freshly malloc'd path; caller frees(). */
char *aide_toolchain_vc_path(const AideToolchainConfig *tc);
char *aide_toolchain_vbccm68k_path(const AideToolchainConfig *tc);
char *aide_toolchain_vasm_path(const AideToolchainConfig *tc);
char *aide_toolchain_vlink_path(const AideToolchainConfig *tc);
char *aide_toolchain_include_dir(const AideToolchainConfig *tc);
char *aide_toolchain_lib_dir(const AideToolchainConfig *tc);

/* ACE BASIC, installed next to vbcc under the same root by the ACE repo's
 * "make -f Makefile-host install": bin/{ace,yap} plus an sdk/ACE tree. */
char *aide_toolchain_ace_path(const AideToolchainConfig *tc);
char *aide_toolchain_yap_path(const AideToolchainConfig *tc);
char *aide_toolchain_ace_sdk_dir(const AideToolchainConfig *tc);
char *aide_toolchain_ace_lib_dir(const AideToolchainConfig *tc);
char *aide_toolchain_ace_include_dir(const AideToolchainConfig *tc);
char *aide_toolchain_ace_bmaps_dir(const AideToolchainConfig *tc);

#ifdef __cplusplus
}
#endif

#endif /* AIDE_TOOLCHAIN_H */
