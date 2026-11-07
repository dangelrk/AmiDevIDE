#ifndef AIDE_SDK_H
#define AIDE_SDK_H

/* Portable C99 mirror of sdk/SdkDescriptor.h + sdk/SdkRegistry.h: the SDK
 * descriptor data model, its .aproj-adjacent JSON (de)serialization, the
 * built-in descriptor table for the shipped sdk/ tree, and the SDKs that
 * describe themselves with an AmiDevSDK.json (the one place here that
 * reads files, through aide_fs). Where sdks.json lives stays in the Qt
 * layer.
 *
 * An SDK is identified by name and version, case-insensitively. Its id is
 * derived from both (aide_sdk_make_id()), and every id comparison here
 * ignores case, so "NDK-3.2" in a project finds "ndk-3.2". */

#include "aide/aide_str.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum AideSdkKind {
    AIDE_SDK_NDK = 0,
    AIDE_SDK_MUI = 1,
    AIDE_SDK_OTHER = 2,
} AideSdkKind;

typedef struct AideSdkDescriptor {
    char *id;
    char *displayName;
    AideSdkKind kind;
    char *version;
    char *rootDir;
    AideStrList includeDirs;
    AideStrList asmIncludeDirs;
    AideStrList libDirs;
    AideStrList autodocDirs;
    AideStrList autodocFiles;
    AideStrList sfdDirs;
    AideStrList suggestedLibs;
    int userDefined; /* bool */
} AideSdkDescriptor;

void aide_sdk_descriptor_init(AideSdkDescriptor *d);
void aide_sdk_descriptor_free(AideSdkDescriptor *d);
void aide_sdk_descriptor_copy(AideSdkDescriptor *dst, const AideSdkDescriptor *src);

/* Serializes one descriptor to a freshly malloc'd JSON object string. */
char *aide_sdk_descriptor_to_json(const AideSdkDescriptor *d);
/* Parses one descriptor from a JSON object; always sets userDefined = 1,
 * matching SdkDescriptor::fromJson(). Returns 1 on success. */
int aide_sdk_descriptor_from_json(AideSdkDescriptor *d, const char *json_text);

typedef struct AideSdkList {
    AideSdkDescriptor *items;
    size_t count;
    size_t cap;
} AideSdkList;

void aide_sdk_list_init(AideSdkList *l);
void aide_sdk_list_free(AideSdkList *l);
void aide_sdk_list_clear(AideSdkList *l);
/* Deep-copies *d into a new slot; caller keeps ownership of *d. */
void aide_sdk_list_push(AideSdkList *l, const AideSdkDescriptor *d);
/* Removes the first descriptor with this id. Returns 1 if one was removed. */
/* Both compare ids case-insensitively, and exactly otherwise: this is the
 * SDK's identity, for adding and removing. To find what a project's id
 * builds against, use aide_sdk_list_resolve(). */
int aide_sdk_list_remove(AideSdkList *l, const char *id);
const AideSdkDescriptor *aide_sdk_list_find(const AideSdkList *l, const char *id);

/* Compares two versions segment by segment -- '.', '-', '_' and spaces
 * separate them -- digits numerically, anything else as text ignoring
 * case, a missing segment counting as 0: "1.10" > "1.9", "2" == "2.0",
 * "5.18" == "5.18". Returns <0, 0 or >0 like strcmp(). */
int aide_sdk_version_compare(const char *a, const char *b);

/* The installed SDK a project's `id` builds against: the SDK with exactly
 * that id if there is one, otherwise the newest SDK of the same name whose
 * version is at least the one the id asks for ("foo-lib-1.2" is served by
 * Foo Lib 1.3, not by 1.1). Case never matters. NULL if nothing serves it.
 * The project keeps its id either way; nothing about the match is written
 * back. */
const AideSdkDescriptor *aide_sdk_list_resolve(const AideSdkList *l, const char *id);

/* The id for an SDK called `name` in `version`: both lowercased, every run
 * of characters other than a-z, 0-9 and '.' turned into one '-', and
 * joined with '-' ("Foo Lib", "1.2" -> "foo-lib-1.2"; "NDK", "3.2" ->
 * "ndk-3.2", the built-in's own id). Freshly malloc'd; "" if name is
 * empty. */
char *aide_sdk_make_id(const char *name, const char *version);

/* File name of the self-description an SDK directory can carry. */
#define AIDE_SDK_MANIFEST "AmiDevSDK.json"

/* Fills *d from the text of an AmiDevSDK.json that lives in `rootDir`:
 *
 *   { "name": "Foo", "version": "1.2", "kind": "other",
 *     "includeDirs": ["include"], "asmIncludeDirs": [], "libDirs": ["lib"],
 *     "autodocDirs": ["Autodocs"], "autodocFiles": [], "sfdDirs": ["SFD"],
 *     "suggestedLibs": ["foo"] }
 *
 * Every path is relative to rootDir and joined onto it ("." is rootDir
 * itself), so the same directory works under SDK: on the Amiga and under
 * toolchain/sdk on the host. The id comes from aide_sdk_make_id(); kind
 * is "ndk", "mui" or "other" (the default); userDefined is set. Returns
 * 0, leaving *d initialized and empty, if the text is not a JSON object or
 * name or version is missing. */
int aide_sdk_descriptor_from_manifest(AideSdkDescriptor *d, const char *json_text, const char *rootDir);

/* Reads every "<sdk_root>/<dir>/AmiDevSDK.json" and appends each valid
 * one whose id *out does not already hold -- so call it after
 * aide_sdk_append_builtins(), and a built-in always wins. Returns how
 * many it appended. */
size_t aide_sdk_append_from_manifests(const char *sdk_root, AideSdkList *out);

/* Appends to *missing every id in `selected` that no SDK in `registry`
 * serves (see aide_sdk_list_resolve()), in their order. Only these stop a
 * project from building: an SDK older than asked for serves nothing. */
void aide_sdk_list_missing(const AideSdkList *registry, const AideStrList *selected, AideStrList *missing);

/* Appends to *out every id in `selected` that is served by an SDK of
 * another major version -- the first version segment differs, so
 * "foo-lib-1.2" served by Foo Lib 3.1.89, not by 1.3.89. Such a project
 * still builds; this is for a warning when it is opened, since the
 * interfaces may have changed between major versions. */
void aide_sdk_list_major_upgrades(const AideSdkList *registry, const AideStrList *selected, AideStrList *out);

/* Appends the built-in descriptors for `sdk_root`: ndk-3.2, mui-3.8, mcc,
 * regex, reqtools, roadshow, amissl, sqlite3, posixlib. Does not clear *out
 * first, so callers can append built-ins after/before user SDKs.
 *
 * There is one NDK (3.2) and one MUI (3.8), on purpose: tst_sdkregistry
 * asserts that no other version comes back.
 *
 * `sdk_root` is the directory that directly contains NDK_3.2/, MUI_3.8/,
 * etc. -- NOT the toolchain root. On the Qt/host side that is
 * `<toolchainRoot>/sdk`; on the native Amiga side the SDKs live under
 * their own "SDK:" assign, unrelated to "vbcc:" (see
 * amiga/src/app/mainwindow.c's toolchain/SDK init). Callers own that
 * distinction; this function just appends "/<Name>" to whatever root
 * it's given. */
void aide_sdk_append_builtins(const char *sdk_root, AideSdkList *out);

/* Serializes every descriptor in *l to a JSON array (used for sdks.json,
 * which only ever holds user-defined SDKs on the Qt side). */
char *aide_sdk_list_to_json_array(const AideSdkList *l);
/* Parses a JSON array of descriptor objects and appends them to *out,
 * each with userDefined forced to 1. Returns 1 on success (even if the
 * array is empty), 0 if json_text isn't a valid JSON array. */
int aide_sdk_list_append_from_json_array(AideSdkList *out, const char *json_text);


/* ---- Is an SDK actually there? ----
 * A descriptor only says where its parts *should* be. aide_sdk_check()
 * looks at each of them on disk, so a frontend can show what an SDK
 * lacks instead of a build failing on a missing include. For a missing
 * part it also looks next to where it should be for a directory or file
 * whose name differs only in case, '_', '-', '.' or blanks ("NDK3.2" for
 * "NDK_3.2", "include_h" for "Include_H") and reports that as `similar`.
 * Data only -- the frontends word it in their own language. */
typedef enum AideSdkPartKind {
    AIDE_SDK_PART_ROOT = 0,
    AIDE_SDK_PART_INCLUDE,
    AIDE_SDK_PART_ASM_INCLUDE,
    AIDE_SDK_PART_LIB,
    AIDE_SDK_PART_AUTODOC_DIR,
    AIDE_SDK_PART_AUTODOC_FILE,
    AIDE_SDK_PART_SFD
} AideSdkPartKind;

typedef struct AideSdkPart {
    AideSdkPartKind kind;
    char *path;
    int present;   /* bool */
    char *similar; /* existing look-alike if !present, else NULL */
} AideSdkPart;

typedef struct AideSdkCheck {
    AideSdkPart *items;
    size_t count;
    size_t cap;
    size_t missing; /* parts with present == 0 */
} AideSdkCheck;

void aide_sdk_check_init(AideSdkCheck *c);
void aide_sdk_check_free(AideSdkCheck *c);
/* Fills *out (cleared first) with the root directory and then every
 * path the descriptor lists, in descriptor order. A missing root makes
 * every part below it missing too; only the root then gets a `similar`. */
void aide_sdk_check(const AideSdkDescriptor *d, AideSdkCheck *out);

#ifdef __cplusplus
}
#endif

#endif /* AIDE_SDK_H */
