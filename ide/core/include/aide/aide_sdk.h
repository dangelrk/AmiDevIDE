#ifndef AIDE_SDK_H
#define AIDE_SDK_H

/* Portable C99 mirror of sdk/SdkDescriptor.h + sdk/SdkRegistry.h: the SDK
 * descriptor data model, its .aproj-adjacent JSON (de)serialization, and
 * the 8 built-in descriptor table for the shipped sdk/ tree. File I/O
 * (sdks.json path resolution, autodoc/SFD directory scanning) stays in
 * the Qt/MUI layers -- this module is pure data + JSON text in/out. */

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
int aide_sdk_list_remove(AideSdkList *l, const char *id);
const AideSdkDescriptor *aide_sdk_list_find(const AideSdkList *l, const char *id);

/* Appends the built-in descriptors for `sdk_root`: ndk-3.2, mui-3.8, mcc,
 * regex, reqtools, roadshow, amissl, sqlite3, posixlib. Does not clear *out
 * first, so callers can append built-ins after/before user SDKs.
 *
 * NDK 3.9 and MUI 5.0 used to be here and are gone on purpose: NDK 3.2 is the
 * newer kit despite the version number, and MUI 5.0 only ships GCC .a
 * libraries that vlink cannot link.
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

#ifdef __cplusplus
}
#endif

#endif /* AIDE_SDK_H */
