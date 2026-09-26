#include "aide/aide_toolchain.h"
#include "aide/aide_str.h"

#include <stdlib.h>

void aide_toolchain_init(AideToolchainConfig *tc, const char *root)
{
    tc->root = aide_strdup(root ? root : "");
}

void aide_toolchain_free(AideToolchainConfig *tc)
{
    free(tc->root);
    tc->root = NULL;
}

const char *aide_toolchain_exe_suffix(void)
{
#if defined(_WIN32) || defined(__CYGWIN__)
    return ".exe";
#else
    return "";
#endif
}

static char *joined(const char *root, const char *suffix)
{
    AideStr s;
    aide_str_init(&s);
    aide_str_append(&s, root);
    aide_str_append_path(&s, suffix);
    return aide_str_take(&s);
}

static char *tool_path(const AideToolchainConfig *tc, const char *rel)
{
    AideStr s;
    aide_str_init(&s);
    aide_str_append(&s, tc->root);
    aide_str_append_path(&s, rel);
    aide_str_append(&s, aide_toolchain_exe_suffix());
    return aide_str_take(&s);
}

char *aide_toolchain_vc_path(const AideToolchainConfig *tc)
{
    AideStr s;
    aide_str_init(&s);
    aide_str_append(&s, tc->root);
    aide_str_append_path(&s, "/bin/vc");
    aide_str_append(&s, aide_toolchain_exe_suffix());
    return aide_str_take(&s);
}

char *aide_toolchain_vbccm68k_path(const AideToolchainConfig *tc)
{
    AideStr s;
    aide_str_init(&s);
    aide_str_append(&s, tc->root);
    aide_str_append_path(&s, "/bin/vbccm68k");
    aide_str_append(&s, aide_toolchain_exe_suffix());
    return aide_str_take(&s);
}

char *aide_toolchain_vasm_path(const AideToolchainConfig *tc)
{
    AideStr s;
    aide_str_init(&s);
    aide_str_append(&s, tc->root);
    aide_str_append_path(&s, "/bin/vasmm68k_mot");
    aide_str_append(&s, aide_toolchain_exe_suffix());
    return aide_str_take(&s);
}

char *aide_toolchain_vlink_path(const AideToolchainConfig *tc)
{
    AideStr s;
    aide_str_init(&s);
    aide_str_append(&s, tc->root);
    aide_str_append_path(&s, "/bin/vlink");
    aide_str_append(&s, aide_toolchain_exe_suffix());
    return aide_str_take(&s);
}

char *aide_toolchain_include_dir(const AideToolchainConfig *tc)
{
    return joined(tc->root, "/targets/m68k-amigaos/include");
}

char *aide_toolchain_lib_dir(const AideToolchainConfig *tc)
{
    return joined(tc->root, "/targets/m68k-amigaos/lib");
}

/* ACE BASIC toolchain, installed alongside vbcc by the ACE repo's
 * src/make/Makefile-host install target: bin/{ace,yap} and an sdk/ACE
 * holding the runtime libraries, .bmap library offset maps, BASIC headers,
 * prototype icon and reusable submodules. */

char *aide_toolchain_ace_path(const AideToolchainConfig *tc)
{
    return tool_path(tc, "/bin/ace");
}

char *aide_toolchain_yap_path(const AideToolchainConfig *tc)
{
    return tool_path(tc, "/bin/yap");
}

char *aide_toolchain_ace_sdk_dir(const AideToolchainConfig *tc)
{
    return joined(tc->root, "/sdk/ACE");
}

char *aide_toolchain_ace_lib_dir(const AideToolchainConfig *tc)
{
    return joined(tc->root, "/sdk/ACE/lib");
}

char *aide_toolchain_ace_include_dir(const AideToolchainConfig *tc)
{
    return joined(tc->root, "/sdk/ACE/include");
}

char *aide_toolchain_ace_bmaps_dir(const AideToolchainConfig *tc)
{
    return joined(tc->root, "/sdk/ACE/bmaps");
}
