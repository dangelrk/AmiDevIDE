#ifndef AIDE_PROJECT_H
#define AIDE_PROJECT_H

/* Portable C99 mirror of the Qt project/Project.h model plus its .aproj
 * JSON (de)serialization (via parson). Field names and JSON keys/defaults
 * match Project.cpp exactly so .aproj stays byte-compatible between the
 * Qt and MUI builds. */

#include "aide/aide_str.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Which compiler drives the project. Not a per-file property: an ACE
 * BASIC program brings its own startup code and runtime (ACE's
 * startup.lib/db.lib) where a C program brings vbcc's (startup.o/vc.lib),
 * and the two cannot be linked into one binary. */
typedef enum AideProjectLanguage {
    AIDE_LANG_C = 0,
    AIDE_LANG_BASIC = 1,
    AIDE_LANG_ASM = 2,
} AideProjectLanguage;

/* What the link step produces. A library is not linked at all: its object
 * files are concatenated, which is what an Amiga link library is -- vc.lib
 * and amiga.lib both start with HUNK_UNIT (0x000003E7), and ACE builds
 * db.lib the same way with `join`. vlink then pulls only the units it
 * needs, which a single relocatable object (vlink -r) would not allow.
 *
 * This is a static link library, not an AmigaOS shared .library -- that is
 * a resident runtime module with its own jump table and needs quite
 * different scaffolding. */
typedef enum AideOutputKind {
    AIDE_OUTPUT_EXECUTABLE = 0,
    AIDE_OUTPUT_LIBRARY = 1,
    /* An AmigaOS shared .library: linked like a program but with no
     * startup object, because exec enters it through the romtag in the
     * source rather than at offset 0. The startup stub must still be the
     * FIRST object on the link line so its "moveq #-1,d0 / rts" lands
     * there and running the library from the Shell fails cleanly --
     * makegen orders it accordingly. See templates/shared-library/. */
    AIDE_OUTPUT_SHARED_LIBRARY = 2,
} AideOutputKind;

typedef enum AideProgramType {
    AIDE_PROGRAM_CLI = 0,
    AIDE_PROGRAM_WB = 1,
} AideProgramType;

typedef enum AideIconMode {
    AIDE_ICON_GENERATE = 0,
    AIDE_ICON_FILE = 1,
} AideIconMode;

typedef enum AideStartup {
    AIDE_STARTUP_STD = 0,
    AIDE_STARTUP_MINSTART = 1,
    AIDE_STARTUP_MINRES = 2,
    AIDE_STARTUP_CUSTOM = 3,
} AideStartup;

typedef struct AideOutputConfig {
    AideOutputKind kind; /* default AIDE_OUTPUT_EXECUTABLE */
    char *binary;     /* "" */
    char *targetDir;  /* default "bin" */
    char *objDir;     /* default "build/obj" */
} AideOutputConfig;

typedef struct AideProgramConfig {
    AideProgramType type;   /* default AIDE_PROGRAM_CLI */
    int stackSize;          /* default 8192 */
    AideIconMode iconMode;  /* default AIDE_ICON_GENERATE */
    char *iconPath;         /* "" unless iconMode == AIDE_ICON_FILE */
} AideProgramConfig;

typedef struct AideCompilerConfig {
    char *cpu;              /* default "68000" */
    char *fpu;               /* default "none" */
    int optLevel;            /* default 2 */
    int smallData;           /* bool; default 0 */
    int debugSymbols;        /* bool; default 0 */
    int kick1hunks;          /* bool; default 1 */
    int asmNoSym;             /* bool; default 1 */
    AideStrList extraCFlags;
    AideStrList extraAsmFlags;
} AideCompilerConfig;

typedef struct AideLinkerConfig {
    AideStartup startup;         /* default AIDE_STARTUP_STD */
    char *customStartupPath;     /* "" */
    AideStrList libraries;       /* ordered: exact link order */
    AideStrList extraLibDirs;
    int strip;                   /* bool; default 0 */
    AideStrList extraLdFlags;
} AideLinkerConfig;

typedef struct AideSdkSelection {
    /* Every SDK the project builds against, by registry id, in the order
     * their include and library directories should be searched.
     *
     * Nothing here is mandatory, the NDK included: a program that talks to
     * the hardware directly needs no system headers at all. A new project
     * starts with just "ndk-3.2". */
    AideStrList selected;
} AideSdkSelection;

typedef struct AideRunConfig {
    int copyToShared;               /* bool; default 1 */
    char *sharedSubdir;             /* "" */
    char *emulatorConfigOverride;   /* "" */
    AideStrList extraFsUaeArgs;
} AideRunConfig;

typedef struct AideEditorState {
    AideStrList openFiles;
    char *activeFile; /* "" */
} AideEditorState;

typedef struct AideProject {
    char *name; /* "" */
    AideProjectLanguage language; /* default AIDE_LANG_C */
    AideOutputConfig output;
    AideProgramConfig program;
    AideStrList sources;      /* each entry a source path, relative to project root */
    AideStrList headers;
    AideStrList includePaths;
    AideStrList defines;
    AideCompilerConfig compiler;
    AideLinkerConfig linker;
    AideSdkSelection sdks;
    AideRunConfig run;
    AideEditorState editor;
} AideProject;

/* Initializes every field to the same defaults Project.h's in-class
 * initializers use (name/version/format defaults, "68000", "ndk-3.2",
 * kick1hunks=1, ...). Must be called before using an AideProject. */
void aide_project_init(AideProject *p);
void aide_project_free(AideProject *p);

/* Serializes to a freshly malloc'd, NUL-terminated JSON string (pretty
 * printed, matching QJsonDocument::toJson()'s default Indented format).
 * `raw_json_passthrough` may be NULL, or a JSON object text containing
 * extra top-level keys to preserve verbatim (forward-compat, mirrors
 * Project::m_rawJson) -- keys also present in AideProject win. */
char *aide_project_to_json(const AideProject *p, const char *raw_json_passthrough);

/* Parses `json_text` (a JSON object) into *p, which must already be
 * aide_project_init()'d (fields not present in the JSON keep their
 * current/default values, exactly like Project::fromJson()). Returns 1
 * on success, 0 if json_text isn't a valid JSON object. */
int aide_project_from_json(AideProject *p, const char *json_text);

/* The same without the editor section: what goes into the .aproj file.
 * The editor state (open files, active file) changes with every session
 * and would keep a versioned project "modified"; it lives in
 * <project>.aproj.user next to it (aide_project_save_user()). A
 * passthrough "editor" key is dropped too. */
char *aide_project_to_file_json(const AideProject *p, const char *raw_json_passthrough);

/* <aproj_path>.user, the per-user side file */
void aide_project_user_path(const char *aproj_path, char *out, size_t outsz);
/* writes / reads only the editor section of <aproj_path>.user; load
 * returns 0 (and changes nothing) if there is none */
int aide_project_save_user(const AideProject *p, const char *aproj_path);
int aide_project_load_user(AideProject *p, const char *aproj_path);

/* Loads/saves the .aproj file at `path` together with its .user side
 * file (aide_project_load() reads it after the .aproj, save writes both).
 * Returns 1 if the .aproj itself was read/written. */
int aide_project_load(AideProject *p, const char *path);
int aide_project_save(const AideProject *p, const char *path);

void aide_project_create_minimal_c(AideProject *p, const char *name);
void aide_project_create_minimal_asm(AideProject *p, const char *name);
void aide_project_create_minimal_basic(AideProject *p, const char *name);

#ifdef __cplusplus
}
#endif

#endif /* AIDE_PROJECT_H */
