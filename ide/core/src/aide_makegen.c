#include "aide/aide_makegen.h"
#include "aide/aide_str.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- small helpers ---- */

static char *abs_path(const char *project_root, const char *relative)
{
    AideStr s;
    if (relative[0] == '\0')
        return aide_strdup("");
    if (relative[0] == '/')
        return aide_strdup(relative);
    aide_str_init(&s);
    aide_str_append(&s, project_root);
    aide_str_append_char(&s, '/');
    aide_str_append(&s, relative);
    return aide_str_take(&s);
}

/* Mirrors QFileInfo(sourceRelPath).completeBaseName() + .path(): splits
 * off everything up to the last '/' as the (relative) directory, and
 * strips only the last '.'-extension from the filename. */
static char *object_file_for(const char *objDir, const char *sourceRelPath)
{
    const char *lastSlash = strrchr(sourceRelPath, '/');
    const char *filename = lastSlash ? lastSlash + 1 : sourceRelPath;
    const char *lastDot = strrchr(filename, '.');
    size_t baseLen = (lastDot && lastDot != filename) ? (size_t)(lastDot - filename) : strlen(filename);
    AideStr s;

    aide_str_init(&s);
    aide_str_append(&s, objDir);
    aide_str_append_char(&s, '/');
    if (lastSlash) {
        aide_str_append_n(&s, sourceRelPath, (size_t)(lastSlash - sourceRelPath));
        aide_str_append_char(&s, '/');
    }
    aide_str_append_n(&s, filename, baseLen);
    aide_str_append(&s, ".o");
    return aide_str_take(&s);
}

static char *startup_object_path(const AideToolchainConfig *tc, const AideProject *project)
{
    char *libDir;
    AideStr s;

    if (project->linker.startup == AIDE_STARTUP_CUSTOM)
        return aide_strdup(project->linker.customStartupPath);

    libDir = aide_toolchain_lib_dir(tc);
    aide_str_init(&s);
    aide_str_append(&s, libDir);
    switch (project->linker.startup) {
    case AIDE_STARTUP_MINSTART: aide_str_append(&s, "/minstart.o"); break;
    case AIDE_STARTUP_MINRES: aide_str_append(&s, "/minres.o"); break;
    case AIDE_STARTUP_STD: default: aide_str_append(&s, "/startup.o"); break;
    }
    free(libDir);
    return aide_str_take(&s);
}

/* Appends "<prefix><item>" for each list entry, space-JOINED (no leading
 * space before the first item) -- mirrors QStringList::join(' ') over a
 * "-X"+item-mapped list (DEFINES/INCLUDES/ASINCLUDES/LDLIBS). */
static void append_prefixed_joined(AideStr *out, const AideStrList *list, const char *prefix)
{
    size_t i;
    for (i = 0; i < list->count; ++i) {
        if (i > 0)
            aide_str_append_char(out, ' ');
        aide_str_append(out, prefix);
        aide_str_append(out, list->items[i]);
    }
}

/* Appends " <prefix><item>" for each list entry (always a leading space)
 * -- mirrors `for (...) s << " -L" << dir;` / `s << ' ' << flag;` loops. */
static void append_prefixed_each(AideStr *out, const AideStrList *list, const char *prefix)
{
    size_t i;
    for (i = 0; i < list->count; ++i) {
        aide_str_append_char(out, ' ');
        aide_str_append(out, prefix);
        aide_str_append(out, list->items[i]);
    }
}

/* AmigaDOS-dialect counterparts: same as the two above, but each whole
 * "<prefix><item>" token is wrapped in double quotes ("-Ipath", never
 * -I"path") -- AmigaDOS only strips quotes when '"' is the very first
 * character of a whitespace-delimited token (see the long precedent
 * comment in amiga/src/app/mainwindow.c around its old quoting fixes).
 * Every one of these tokens can carry an absolute SDK/toolchain path,
 * and while "SDK:"/"vbcc:" are chosen to be space-free by convention,
 * both roots become user-configurable (see prefswindow.c) -- quoting
 * here is what keeps a user-chosen root with a space in it from being
 * re-split by vc's/vasm's/vlink's own command-line frontend. */
static void append_prefixed_joined_quoted(AideStr *out, const AideStrList *list, const char *prefix)
{
    size_t i;
    for (i = 0; i < list->count; ++i) {
        if (i > 0)
            aide_str_append_char(out, ' ');
        aide_str_append_char(out, '"');
        aide_str_append(out, prefix);
        aide_str_append(out, list->items[i]);
        aide_str_append_char(out, '"');
    }
}

static void append_prefixed_each_quoted(AideStr *out, const AideStrList *list, const char *prefix)
{
    size_t i;
    for (i = 0; i < list->count; ++i) {
        aide_str_append_char(out, ' ');
        aide_str_append_char(out, '"');
        aide_str_append(out, prefix);
        aide_str_append(out, list->items[i]);
        aide_str_append_char(out, '"');
    }
}

/* ---- public API ---- */

AideMakeDialect aide_makegen_host_dialect(void)
{
    /* __M68K__ is vbcc's own predefined macro for this target (verified:
     * defined when core/ is compiled for m68k-amigaos, absent on the
     * host x86/arm64 build) -- same criterion aide_vccfg.c:61 already
     * uses to pick vc.cfg's -rm=/-rmv= directives ("which host will vc
     * run on", not "which side generated this file"). core/'s only m68k
     * target is this one, so it doubles as "which make dialect applies"
     * exactly. */
#if defined(__M68K__)
    return AIDE_MAKE_DIALECT_AMIGADOS;
#else
    return AIDE_MAKE_DIALECT_POSIX;
#endif
}

int aide_makegen_needs_stack_object(const AideProject *project)
{
    /* __stack is a vbcc/vc.lib convention, read by vbcc's startup code.
     * ACE brings its own startup.lib, which neither defines nor consults
     * it, and compiling a C stub for it would drag vc.lib into an
     * otherwise pure-BASIC link. */
    if (project->language == AIDE_LANG_BASIC)
        return 0;
    /* A library has no startup code of its own, and defining __stack in it
     * would collide with the program that links it. */
    if (project->output.kind == AIDE_OUTPUT_LIBRARY
        || project->output.kind == AIDE_OUTPUT_SHARED_LIBRARY)
        return 0;
    return project->program.type == AIDE_PROGRAM_CLI && project->linker.startup == AIDE_STARTUP_STD;
}

char *aide_makegen_stack_object_source(int stack_size)
{
    char buf[128];
    snprintf(buf, sizeof(buf), "#include <stddef.h>\nsize_t __stack = %d;\n", stack_size);
    return aide_strdup(buf);
}

char *aide_makegen_target_binary_path(const AideProject *project, const char *project_root)
{
    AideStr s;
    char *targetDir = abs_path(project_root, project->output.targetDir);
    aide_str_init(&s);
    aide_str_append(&s, targetDir);
    aide_str_append_char(&s, '/');
    aide_str_append(&s, project->output.binary);
    free(targetDir);
    return aide_str_take(&s);
}

/* Which of a BASIC project's sources is the main program?
 *
 * ACE compiles a module with -m (no startup code) and the program without
 * it; getting this wrong makes every source emit its own _EXIT_PROG and
 * the link fails on the duplicate. There is no marker in the project
 * model, so the rule is: the source whose base name matches the output
 * binary, else the first one listed. */
static size_t basic_main_source_index(const AideProject *project)
{
    size_t i;

    for (i = 0; i < project->sources.count; ++i) {
        const char *rel = project->sources.items[i];
        const char *lastSlash = strrchr(rel, '/');
        const char *filename = lastSlash ? lastSlash + 1 : rel;
        const char *lastDot = strrchr(filename, '.');
        size_t baseLen = (lastDot && lastDot != filename) ? (size_t)(lastDot - filename)
                                                          : strlen(filename);
        if (strlen(project->output.binary) == baseLen
            && strncmp(filename, project->output.binary, baseLen) == 0)
            return i;
    }
    return 0;
}

char *aide_makegen_generate(const AideToolchainConfig *toolchain, const AideProject *project,
                             const char *project_root, const AideSdkList *sdk_registry)
{
    return aide_makegen_generate_dialect(toolchain, project, project_root, sdk_registry,
                                          AIDE_MAKE_DIALECT_POSIX);
}

char *aide_makegen_generate_dialect(const AideToolchainConfig *toolchain, const AideProject *project,
                                     const char *project_root, const AideSdkList *sdk_registry,
                                     AideMakeDialect dialect)
{
    AideStrList includeDirs, asmIncludeDirs, libDirs, defineFlags, ldlibFlags, objectPaths, headerDeps;
    AideStr s;
    size_t i;
    int stackObj;
    char *objDir, *binDir, *targetPath, *startup, *fpuFlag;
    const char *exeSuffix = aide_toolchain_exe_suffix();
    const int amiga = (dialect == AIDE_MAKE_DIALECT_AMIGADOS);
    const int basic = (project->language == AIDE_LANG_BASIC);
    /* ACE's default is 68020 (parsevar.c cpu020_opt). The host runs our
     * patched ACE, which takes -0 for 68000; the Amiga runs ACE as
     * shipped, which only knows "OPTION 2-" in the source -- see the
     * BASIC compile rule below. */
    const int cpu68000 = (strcmp(project->compiler.cpu, "68000") == 0);
    /* Set with ACEFLAGS; an empty one changes how modules are compiled. */
    int aceFlagsEmpty = 0;
    /* A static library is concatenated; a shared .library is linked, but
     * without a startup object -- exec enters it through its romtag. */
    const int library = (project->output.kind == AIDE_OUTPUT_LIBRARY);
    const int sharedLib = (project->output.kind == AIDE_OUTPUT_SHARED_LIBRARY);
    /* Plain "=" for the AmigaDOS dialect, not ":=" -- purely a style
     * choice (every value here is a literal string or a fixed reference
     * to an already-defined variable, never a $(shell ...)/$(wildcard
     * ...) call whose *expansion time* would matter), made to remove
     * all doubt rather than lean on gnu_make_amiga's colon-adjacent-to-
     * whitespace heuristic to also cover "::"-vs-":=" edge cases. */
    const char *assignOp = amiga ? " = " : " := ";

    aide_strlist_init(&includeDirs);
    aide_strlist_init(&asmIncludeDirs);
    aide_strlist_init(&libDirs);
    aide_strlist_init(&defineFlags);
    aide_strlist_init(&ldlibFlags);
    aide_strlist_init(&objectPaths);
    aide_strlist_init(&headerDeps);

    for (i = 0; i < project->includePaths.count; ++i) {
        char *p = amiga ? aide_strdup(project->includePaths.items[i])
                         : abs_path(project_root, project->includePaths.items[i]);
        aide_strlist_push_owned(&includeDirs, p);
    }

    {
        const AideStrList *ids = &project->sdks.selected;

        for (i = 0; i < ids->count; ++i) {
            const AideSdkDescriptor *sdk = aide_sdk_list_resolve(sdk_registry, ids->items[i]);
            size_t j;
            if (!sdk)
                continue;
            for (j = 0; j < sdk->includeDirs.count; ++j)
                aide_strlist_push(&includeDirs, sdk->includeDirs.items[j]);
            for (j = 0; j < sdk->asmIncludeDirs.count; ++j)
                aide_strlist_push(&asmIncludeDirs, sdk->asmIncludeDirs.items[j]);
            for (j = 0; j < sdk->libDirs.count; ++j)
                aide_strlist_push(&libDirs, sdk->libDirs.items[j]);
        }
    }
    /* asm sources may include local project headers too */
    for (i = 0; i < includeDirs.count; ++i)
        aide_strlist_push(&asmIncludeDirs, includeDirs.items[i]);

    for (i = 0; i < project->linker.extraLibDirs.count; ++i) {
        char *p = amiga ? aide_strdup(project->linker.extraLibDirs.items[i])
                         : abs_path(project_root, project->linker.extraLibDirs.items[i]);
        aide_strlist_push_owned(&libDirs, p);
    }

    for (i = 0; i < project->defines.count; ++i) {
        AideStr d;
        aide_str_init(&d);
        aide_str_append(&d, "-D");
        aide_str_append(&d, project->defines.items[i]);
        aide_strlist_push_owned(&defineFlags, aide_str_take(&d));
    }

    fpuFlag = aide_strdup("");
    if (strcmp(project->compiler.fpu, "none") != 0 && strcmp(project->compiler.fpu, "softfloat") != 0) {
        AideStr f;
        aide_str_init(&f);
        aide_str_append(&f, " -fpu=");
        aide_str_append(&f, project->compiler.fpu);
        free(fpuFlag);
        fpuFlag = aide_str_take(&f);
    }

    /* AmigaDOS dialect: no project_root join at all -- OBJDIR/BINDIR/
     * TARGET are emitted exactly as configured (already relative to the
     * project root, see aide_project.h), on the assumption that `make`
     * itself runs with its current directory set to the project root
     * (see aide_makegen.h's dialect doc comment). POSIX dialect:
     * unchanged, always absolute. */
    if (amiga) {
        objDir = aide_strdup(project->output.objDir);
        binDir = aide_strdup(project->output.targetDir);
        {
            AideStr t;
            aide_str_init(&t);
            aide_str_append(&t, binDir);
            aide_str_append_char(&t, '/');
            aide_str_append(&t, project->output.binary);
            targetPath = aide_str_take(&t);
        }
    } else {
        objDir = abs_path(project_root, project->output.objDir);
        binDir = abs_path(project_root, project->output.targetDir);
        targetPath = aide_makegen_target_binary_path(project, project_root);
    }

    aide_str_init(&s);
    aide_str_append(&s, "# Generated by AmigaIDE -- DO NOT EDIT (regenerated on every build)\n");
    aide_str_append(&s, "VBCC ?= "); aide_str_append(&s, toolchain->root); aide_str_append_char(&s, '\n');
    aide_str_append(&s, "E    ?= "); aide_str_append(&s, exeSuffix); aide_str_append_char(&s, '\n');
    if (amiga) {
        /* No leading '/' after $(VBCC): a root ending in ':' (e.g.
         * "vbcc:") is an AmigaDOS assign, and "vbcc:/bin/vc" means
         * "the bin/vc in vbcc:'s *parent*", not "vbcc:bin/vc" -- see
         * aide_str_append_path()'s own doc comment for the same rule. */
        if (basic) {
            /* ACE as shipped, where its own installer and bin/bas put
             * it: ACE:bin, ACElib:, ACEinclude:. Only the host runs the
             * patched build from compilers/acebasic/. */
            aide_str_append(&s, "ACE  = ACE:bin/ace\n");
            aide_str_append(&s, "YAP  = ACE:bin/yap\n");
        } else {
            aide_str_append(&s, "VC   = $(VBCC)bin/vc$(E)\n");
        }
        aide_str_append(&s, "VASM = $(VBCC)bin/vasmm68k_mot$(E)\n");
        aide_str_append(&s, "VLINK= $(VBCC)bin/vlink$(E)\n");
        if (basic) {
            aide_str_append(&s, "ACE_LIB = ACElib:\n");
        } else {
            aide_str_append(&s, "VBCC_LIB = $(VBCC)targets/m68k-amigaos/lib\n");
        }
    } else {
        if (basic) {
            aide_str_append(&s, "ACE  := $(VBCC)/bin/ace$(E)\n");
            aide_str_append(&s, "YAP  := $(VBCC)/bin/yap$(E)\n");
        } else {
            aide_str_append(&s, "VC   := $(VBCC)/bin/vc$(E)\n");
        }
        aide_str_append(&s, "VASM := $(VBCC)/bin/vasmm68k_mot$(E)\n");
        aide_str_append(&s, "VLINK:= $(VBCC)/bin/vlink$(E)\n");
        if (basic) {
            aide_str_append(&s, "ACE_SDK := $(VBCC)/sdk/ACE\n");
            aide_str_append(&s, "ACE_LIB := $(ACE_SDK)/lib\n");
        } else {
            aide_str_append(&s, "VBCC_LIB := $(VBCC)/targets/m68k-amigaos/lib\n");
        }
    }
    aide_str_append(&s, "\n");

    aide_str_append(&s, "OBJDIR"); aide_str_append(&s, assignOp); aide_str_append(&s, objDir); aide_str_append_char(&s, '\n');
    aide_str_append(&s, "BINDIR"); aide_str_append(&s, assignOp); aide_str_append(&s, binDir); aide_str_append_char(&s, '\n');
    aide_str_append(&s, "TARGET"); aide_str_append(&s, assignOp); aide_str_append(&s, targetPath); aide_str_append_char(&s, '\n');
    aide_str_append(&s, "\n");

    aide_str_append(&s, "DEFINES"); aide_str_append(&s, assignOp);
    if (amiga) append_prefixed_joined_quoted(&s, &defineFlags, "");
    else       append_prefixed_joined(&s, &defineFlags, "");
    aide_str_append_char(&s, '\n');
    aide_str_append(&s, "INCLUDES"); aide_str_append(&s, assignOp);
    if (amiga) append_prefixed_joined_quoted(&s, &includeDirs, "-I");
    else       append_prefixed_joined(&s, &includeDirs, "-I");
    aide_str_append_char(&s, '\n');
    aide_str_append(&s, "ASINCLUDES"); aide_str_append(&s, assignOp);
    if (amiga) append_prefixed_joined_quoted(&s, &asmIncludeDirs, "-I");
    else       append_prefixed_joined(&s, &asmIncludeDirs, "-I");
    aide_str_append_char(&s, '\n');
    aide_str_append(&s, "\n");

    if (basic) {
        /* yap: on the host -g, so ace's diagnostics name the original
         * source rather than the preprocessed text in OBJDIR (patched
         * yap.c), and the ACE BASIC headers from the SDK. The Amiga's yap
         * has no -g and finds <...> includes in ACEinclude: by itself. */
        aide_str_append(&s, "YAPFLAGS"); aide_str_append(&s, assignOp);
        if (amiga)
            aide_str_append(&s, "$(DEFINES) $(INCLUDES)");
        else
            aide_str_append(&s, "-g $(DEFINES) $(INCLUDES) -I$(ACE_SDK)/include");
        aide_str_append_char(&s, '\n');

        /* ace accepts at most one option argument and reads it as a
         * bundle of single letters (ACE repo, parse.c check_options and
         * its argc == 3 limit), so everything goes into one token:
         * "-2O", not "-2 -O". Extra flags contribute their letters. */
        {
            AideStr letters;
            aide_str_init(&letters);
            /* The Amiga's ace has no -0; 68000 there means leaving out
             * the 2 and prepending "OPTION 2-" (compile rule below). */
            if (cpu68000)
                aide_str_append(&letters, amiga ? "" : "0");
            else
                aide_str_append_char(&letters, '2');
            if (project->compiler.optLevel > 0)
                aide_str_append_char(&letters, 'O');
            for (i = 0; i < project->compiler.extraCFlags.count; ++i) {
                const char *f = project->compiler.extraCFlags.items[i];
                while (*f == '-') ++f;
                aide_str_append(&letters, f);
            }
            aceFlagsEmpty = (letters.len == 0);

            aide_str_append(&s, "ACEFLAGS"); aide_str_append(&s, assignOp);
            if (!aceFlagsEmpty) {
                aide_str_append_char(&s, '-');
                aide_str_append(&s, letters.data);
            }
            aide_str_append_char(&s, '\n');
            aide_str_free(&letters);
        }
    } else {
    aide_str_append(&s, "CFLAGS "); aide_str_append(&s, assignOp);
    aide_str_append(&s, amiga ? "\"+build/vc.cfg\" -c99 -cpu=" : "+$(abspath build/vc.cfg) -c99 -cpu=");
    aide_str_append(&s, project->compiler.cpu);
    aide_str_append(&s, " -O");
    aide_str_appendf(&s, "%d", project->compiler.optLevel);
    aide_str_append(&s, fpuFlag);
    if (project->compiler.debugSymbols)
        aide_str_append(&s, " -g");
    aide_str_append(&s, " $(DEFINES) $(INCLUDES)");
    for (i = 0; i < project->compiler.extraCFlags.count; ++i) {
        aide_str_append_char(&s, ' ');
        aide_str_append(&s, project->compiler.extraCFlags.items[i]);
    }
    aide_str_append_char(&s, '\n');

    }

    aide_str_append(&s, "ASFLAGS"); aide_str_append(&s, assignOp);
    aide_str_append(&s, "-Fhunk -m");
    aide_str_append(&s, project->compiler.cpu);
    if (project->compiler.kick1hunks)
        aide_str_append(&s, " -kick1hunks");
    if (project->compiler.asmNoSym)
        aide_str_append(&s, " -nosym");
    aide_str_append(&s, " $(ASINCLUDES)");
    for (i = 0; i < project->compiler.extraAsmFlags.count; ++i) {
        aide_str_append_char(&s, ' ');
        aide_str_append(&s, project->compiler.extraAsmFlags.items[i]);
    }
    aide_str_append_char(&s, '\n');

    aide_str_append(&s, "LDFLAGS"); aide_str_append(&s, assignOp);
    /* -Cvbcc names vbcc's constructor/destructor convention; ACE has none
     * of its own, and bin/bas does not pass it either. */
    aide_str_append(&s, basic ? "-bamigahunk -x -Bstatic -nostdlib -mrel "
                              : "-bamigahunk -x -Bstatic -Cvbcc -nostdlib -mrel ");
    if (basic)
        aide_str_append(&s, amiga ? "\"-L$(ACE_LIB)\"" : "-L$(ACE_LIB)");
    else
        aide_str_append(&s, amiga ? "\"-L$(VBCC_LIB)\"" : "-L$(VBCC_LIB)");
    if (amiga) append_prefixed_each_quoted(&s, &libDirs, "-L");
    else       append_prefixed_each(&s, &libDirs, "-L");
    if (project->linker.strip)
        aide_str_append(&s, " -s -Rshort");
    for (i = 0; i < project->linker.extraLdFlags.count; ++i) {
        aide_str_append_char(&s, ' ');
        aide_str_append(&s, project->linker.extraLdFlags.items[i]);
    }
    aide_str_append_char(&s, '\n');

    if (basic) {
        /* ACE's runtime, not vbcc's: startup.lib carries the startup code
         * ace's output jumps into (_startup/_cleanup/_ABORT_PROG),
         * db.lib the BASIC runtime, amiga.lib the shared-library stubs.
         * They are hunk-object concatenations, not -l archives, so they go
         * on the link line by path -- the same set and order bin/bas
         * uses. The 68000 build has its own startup.lib/db.lib and needs
         * vc.lib on top for the software float routines. On the Amiga
         * ACE_LIB is the ACElib: assign, which takes no '/' after it. */
        const char *libdir = amiga ? "$(ACE_LIB)" : "$(ACE_LIB)/";
        AideStr l;

        aide_str_init(&l);
        aide_str_append(&l, libdir);
        aide_str_append(&l, cpu68000 ? "68000/startup.lib" : "startup.lib");
        startup = aide_str_take(&l);

        aide_str_init(&l);
        aide_str_append(&l, libdir);
        aide_str_append(&l, cpu68000 ? "68000/db.lib" : "db.lib");
        aide_strlist_push_owned(&ldlibFlags, aide_str_take(&l));

        aide_str_init(&l);
        aide_str_append(&l, libdir);
        aide_str_append(&l, "amiga.lib");
        aide_strlist_push_owned(&ldlibFlags, aide_str_take(&l));

        if (cpu68000) {
            aide_str_init(&l);
            aide_str_append(&l, libdir);
            aide_str_append(&l, "vc.lib");
            aide_strlist_push_owned(&ldlibFlags, aide_str_take(&l));
        }

        /* project-configured libraries still apply, as -l names */
        for (i = 0; i < project->linker.libraries.count; ++i) {
            aide_str_init(&l);
            aide_str_append(&l, "-l");
            aide_str_append(&l, project->linker.libraries.items[i]);
            aide_strlist_push_owned(&ldlibFlags, aide_str_take(&l));
        }
    } else {
        startup = startup_object_path(toolchain, project);

        for (i = 0; i < project->linker.libraries.count; ++i) {
            AideStr l;
            aide_str_init(&l);
            aide_str_append(&l, "-l");
            aide_str_append(&l, project->linker.libraries.items[i]);
            aide_strlist_push_owned(&ldlibFlags, aide_str_take(&l));
        }
    }
    /* A library is concatenated, never linked, so a STARTUP/LDLIBS here
     * would be dead weight that only invites confusion. */
    if (!library && !sharedLib) {
        aide_str_append(&s, "STARTUP"); aide_str_append(&s, assignOp); aide_str_append(&s, startup); aide_str_append_char(&s, '\n');
    }
    if (!library) {
        aide_str_append(&s, "LDLIBS "); aide_str_append(&s, assignOp);
        append_prefixed_joined(&s, &ldlibFlags, "");
        aide_str_append_char(&s, '\n');
    }
    aide_str_append(&s, "\n");

    /* For a shared .library the startup stub has to be the first object on
     * the link line: its "moveq #-1,d0 / rts" must land at offset 0 of the
     * segment so running the library from the Shell fails cleanly instead
     * of jumping into library code. Recognised by base name rather than by
     * list position, so reordering sources in the UI cannot silently break
     * the binary. */
    if (sharedLib) {
        for (i = 0; i < project->sources.count; ++i) {
            const char *rel = project->sources.items[i];
            const char *slash = strrchr(rel, '/');
            const char *fn = slash ? slash + 1 : rel;
            if (strncmp(fn, "libstart.", 9) == 0)
                aide_strlist_push_owned(&objectPaths,
                                        object_file_for("$(OBJDIR)", rel));
        }
    }
    for (i = 0; i < project->sources.count; ++i) {
        const char *rel = project->sources.items[i];
        const char *slash = strrchr(rel, '/');
        const char *fn = slash ? slash + 1 : rel;
        if (sharedLib && strncmp(fn, "libstart.", 9) == 0)
            continue;   /* already emitted first */
        aide_strlist_push_owned(&objectPaths, object_file_for("$(OBJDIR)", rel));
    }
    stackObj = aide_makegen_needs_stack_object(project);
    if (stackObj)
        aide_strlist_push(&objectPaths, "$(OBJDIR)/__stack.o");

    aide_str_append(&s, "OBJS"); aide_str_append(&s, assignOp);
    append_prefixed_joined(&s, &objectPaths, "");
    aide_str_append_char(&s, '\n');
    aide_str_append(&s, "\n");

    aide_str_append(&s, amiga ? "all : $(TARGET)\n" : "all: $(TARGET)\n");
    if (amiga)
        /* Phony recipe -- runs on every successful `make all`, including
         * "nothing to be done" (semantically correct: the build is still
         * up to date), but not if $(TARGET) failed. No AmigaDOS
         * metacharacters in the text ('#'/'?' are pattern chars, '>' is
         * redirection) -- picked up by the async build launcher's
         * exit-status detection (see mainwindow.c's build_tick()). */
        aide_str_append(&s, "\t@Echo \"--AIDE-BUILD-OK--\"\n");
    aide_str_append(&s, "\n");

    aide_str_append(&s, amiga ? "$(TARGET) : $(OBJS)\n" : "$(TARGET): $(OBJS)\n");
    aide_str_append(&s, amiga ? "\t@-makedir \"$(BINDIR)\"\n" : "\t@mkdir -p \"$(BINDIR)\"\n");
    if (library) {
        /*
         * An Amiga link library is its object files concatenated: vc.lib
         * and amiga.lib both begin with HUNK_UNIT (0x000003E7), and
         * vlink pulls out only the units a program actually references.
         * AmigaDOS spells the concatenation `join`, POSIX spells it
         * `cat` -- ACE's own Makefile-lib uses the former for db.lib.
         *
         * Deliberately not `vlink -r`: that merges everything into one
         * relocatable object, so every user would drag in the whole
         * library instead of the parts they call.
         */
        if (amiga) {
            aide_str_append(&s, "\t@-delete \"$@\" quiet\n");
            aide_str_append(&s, "\tjoin $(OBJS) AS \"$@\"\n");
        } else {
            aide_str_append(&s, "\tcat $(OBJS) > \"$@\"\n");
        }
    } else if (sharedLib) {
        /* No startup object: exec finds the romtag, and offset 0 of the
         * segment is the stub's "moveq #-1,d0 / rts" -- see the OBJS
         * ordering above for how that stub is kept first. */
        aide_str_append(&s, "\t$(VLINK) $(LDFLAGS) $(OBJS) $(LDLIBS) -o \"$@\"\n");
    } else {
        aide_str_append(&s, "\t$(VLINK) $(LDFLAGS) $(STARTUP) $(OBJS) $(LDLIBS) -o \"$@\"\n");
    }
    aide_str_append(&s, "\n");

    for (i = 0; i < project->headers.count; ++i) {
        char *p = amiga ? aide_strdup(project->headers.items[i])
                        : abs_path(project_root, project->headers.items[i]);
        aide_strlist_push_owned(&headerDeps, p);
    }

    /* Progress line count includes __stack.o (if any) -- it's a real
     * compile step too, just not one of project->sources. Printed
     * before each compile recipe so `make` runs (and their absence, on
     * a second incremental build) are visible without reading the full
     * vc/vasm command line -- a gap in the numbering shows exactly
     * which files `make` decided were already up to date. */
    {
        size_t totalUnits = project->sources.count + (stackObj ? 1 : 0);
        size_t unitIndex = 0;
        const size_t basicMainIndex = basic ? basic_main_source_index(project) : 0;

    for (i = 0; i < project->sources.count; ++i) {
        const char *relPath = project->sources.items[i];
        char *absSrc = amiga ? aide_strdup(relPath) : abs_path(project_root, relPath);
        char *objPath = object_file_for("$(OBJDIR)", relPath);
        const char *lastSlash = strrchr(relPath, '/');
        const char *filename = lastSlash ? lastSlash + 1 : relPath;
        const char *lastDot = strrchr(filename, '.');
        int isAsm = 0;
        int isBasic = 0;
        /* Only the main program gets startup code; every other BASIC
         * source is a module, compiled with -m. */
        const int isBasicMain = (i == basicMainIndex);
        size_t j;

        ++unitIndex;

        if (lastDot) {
            char suffix[16];
            size_t suffixLen = strlen(lastDot + 1);
            if (suffixLen < sizeof(suffix)) {
                for (j = 0; j < suffixLen; ++j)
                    suffix[j] = (char)tolower((unsigned char)lastDot[1 + j]);
                suffix[suffixLen] = '\0';
                isAsm = strcmp(suffix, "s") == 0 || strcmp(suffix, "asm") == 0 || strcmp(suffix, "i") == 0;
                isBasic = strcmp(suffix, "b") == 0 || strcmp(suffix, "bas") == 0;
            }
        }

        aide_str_append(&s, objPath);
        aide_str_append(&s, amiga ? " : " : ": ");
        aide_str_append(&s, absSrc);
        for (j = 0; j < headerDeps.count; ++j) {
            aide_str_append_char(&s, ' ');
            aide_str_append(&s, headerDeps.items[j]);
        }
        aide_str_append_char(&s, '\n');
        aide_str_append(&s, amiga ? "\t@-makedir \"$(@D)\"\n" : "\t@mkdir -p \"$(@D)\"\n");
        aide_str_append(&s, amiga ? "\t@Echo \"[" : "\t@echo \"[");
        aide_str_appendf(&s, "%d/%d] %s\"\n", (int)unitIndex, (int)totalUnits, filename);
        if (isBasic) {
            /* Three stages, all writing into OBJDIR: yap preprocesses,
             * ace compiles the result to 68k assembly, vasm assembles it.
             * bas(1) is deliberately not used -- it owns its own temp
             * directory, #using scan and link line, which would fight the
             * per-object rules and OBJDIR discipline the rest of this
             * Makefile is built on.
             *
             * The intermediate keeps the object's base name with a .b
             * suffix; ace derives its own output name from it and takes
             * the extension from the last dot, so a source called
             * "a.b.b" is handled correctly too. */
            char *ppBase = object_file_for("$(OBJDIR)", relPath);
            size_t ppLen = strlen(ppBase);
            ppBase[ppLen - 2] = '\0';          /* drop the ".o" */

            aide_str_append(&s, "\t$(YAP) $(YAPFLAGS) \"");
            aide_str_append(&s, absSrc);
            aide_str_append(&s, "\" \"");
            aide_str_append(&s, ppBase);
            if (amiga && cpu68000) {
                /* ACE as shipped only switches to 68000 code through the
                 * OPTION metacommand, so it goes in front of the
                 * preprocessed text; the source itself stays untouched. */
                aide_str_append(&s, ".yap\"\n");
                aide_str_append(&s, "\tEcho >\""); aide_str_append(&s, ppBase);
                aide_str_append(&s, ".opt\" \"OPTION 2-\"\n");
                aide_str_append(&s, "\tJoin \""); aide_str_append(&s, ppBase);
                aide_str_append(&s, ".opt\" \""); aide_str_append(&s, ppBase);
                aide_str_append(&s, ".yap\" AS \""); aide_str_append(&s, ppBase);
                aide_str_append(&s, ".b\"\n");
            } else {
                aide_str_append(&s, ".b\"\n");
            }

            /* ace takes one option bundle; with none, a module still
             * needs its m. */
            aide_str_append(&s, isBasicMain ? "\t$(ACE) $(ACEFLAGS) \""
                              : aceFlagsEmpty ? "\t$(ACE) -m \""
                                              : "\t$(ACE) $(ACEFLAGS)m \"");
            aide_str_append(&s, ppBase);
            aide_str_append(&s, ".b\"\n");

            aide_str_append(&s, "\t$(VASM) $(ASFLAGS) -o \"$@\" \"");
            aide_str_append(&s, ppBase);
            aide_str_append(&s, ".s\"\n");

            free(ppBase);
        } else if (isAsm) {
            aide_str_append(&s, "\t$(VASM) $(ASFLAGS) -o \"$@\" \"");
            aide_str_append(&s, absSrc);
            aide_str_append(&s, "\"\n");
        } else {
            aide_str_append(&s, "\t$(VC) $(CFLAGS) -c \"");
            aide_str_append(&s, absSrc);
            aide_str_append(&s, "\" -o \"$@\"\n");
        }
        aide_str_append(&s, "\n");

        free(absSrc);
        free(objPath);
    }

    if (stackObj) {
        char *stackPathStr;
        if (amiga) {
            stackPathStr = aide_strdup("build/__stack.c");
        } else {
            AideStr stackPath;
            aide_str_init(&stackPath);
            aide_str_append(&stackPath, project_root);
            aide_str_append(&stackPath, "/build/__stack.c");
            stackPathStr = aide_str_take(&stackPath);
        }

        ++unitIndex;

        aide_str_append(&s, "$(OBJDIR)/__stack.o"); aide_str_append(&s, amiga ? " : " : ": ");
        aide_str_append(&s, stackPathStr);
        aide_str_append_char(&s, '\n');
        aide_str_append(&s, amiga ? "\t@-makedir \"$(@D)\"\n" : "\t@mkdir -p \"$(@D)\"\n");
        aide_str_append(&s, amiga ? "\t@Echo \"[" : "\t@echo \"[");
        aide_str_appendf(&s, "%d/%d] __stack.c\"\n", (int)unitIndex, (int)totalUnits);
        aide_str_append(&s, "\t$(VC) $(CFLAGS) -c \"");
        aide_str_append(&s, stackPathStr);
        aide_str_append(&s, "\" -o \"$@\"\n");
        aide_str_append(&s, "\n");
        free(stackPathStr);
    }
    } /* totalUnits/unitIndex scope */

    if (amiga) {
        aide_str_append(&s, "clean :\n");
        aide_str_append(&s, "\t@-delete \"$(OBJDIR)\" all quiet\n");
        aide_str_append(&s, "\t@-delete \"$(TARGET)\" quiet\n");
    } else {
        aide_str_append(&s, "clean:\n");
        aide_str_append(&s, "\trm -rf \"$(OBJDIR)\" \"$(TARGET)\"\n");
    }
    aide_str_append(&s, "\n");

    aide_str_append(&s, amiga ? ".PHONY : all clean\n" : ".PHONY: all clean\n");

    free(objDir);
    free(binDir);
    free(targetPath);
    free(startup);
    free(fpuFlag);
    aide_strlist_free(&includeDirs);
    aide_strlist_free(&asmIncludeDirs);
    aide_strlist_free(&libDirs);
    aide_strlist_free(&defineFlags);
    aide_strlist_free(&ldlibFlags);
    aide_strlist_free(&objectPaths);
    aide_strlist_free(&headerDeps);

    return aide_str_take(&s);
}
