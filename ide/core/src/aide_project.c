#include "aide/aide_project.h"

#include "parson.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- small helpers ---- */

static void strset(char **field, const char *value)
{
    free(*field);
    *field = aide_strdup(value ? value : "");
}

static const char *jstr(const JSON_Object *obj, const char *key, const char *def)
{
    const char *v;
    if (!obj)
        return def;
    v = json_object_get_string(obj, key);
    return v ? v : def;
}

static int jbool(const JSON_Object *obj, const char *key, int def)
{
    if (!obj || !json_object_has_value_of_type(obj, key, JSONBoolean))
        return def;
    return json_object_get_boolean(obj, key) ? 1 : 0;
}

static int jint(const JSON_Object *obj, const char *key, int def)
{
    if (!obj || !json_object_has_value_of_type(obj, key, JSONNumber))
        return def;
    return (int)json_object_get_number(obj, key);
}

static void jstrlist(const JSON_Object *obj, const char *key, AideStrList *out)
{
    JSON_Array *arr;
    size_t i, n;

    aide_strlist_clear(out);
    if (!obj)
        return;
    arr = json_object_get_array(obj, key);
    if (!arr)
        return;
    n = json_array_get_count(arr);
    for (i = 0; i < n; ++i) {
        const char *s = json_array_get_string(arr, i);
        aide_strlist_push(out, s ? s : "");
    }
}

static JSON_Value *mk_strlist_array(const AideStrList *list)
{
    JSON_Value *v = json_value_init_array();
    JSON_Array *arr = json_value_get_array(v);
    size_t i;
    for (i = 0; i < list->count; ++i)
        json_array_append_string(arr, list->items[i]);
    return v;
}

static const char *language_to_string(AideProjectLanguage l)
{
    if (l == AIDE_LANG_BASIC) return "basic";
    if (l == AIDE_LANG_ASM) return "asm";
    return "c";
}

static AideProjectLanguage language_from_string(const char *s)
{
    if (s && strcmp(s, "basic") == 0) return AIDE_LANG_BASIC;
    if (s && strcmp(s, "asm") == 0) return AIDE_LANG_ASM;
    return AIDE_LANG_C;
}

static const char *output_kind_to_string(AideOutputKind k)
{
    if (k == AIDE_OUTPUT_LIBRARY) return "library";
    if (k == AIDE_OUTPUT_SHARED_LIBRARY) return "sharedlibrary";
    return "executable";
}

static AideOutputKind output_kind_from_string(const char *s)
{
    if (s && strcmp(s, "library") == 0) return AIDE_OUTPUT_LIBRARY;
    if (s && strcmp(s, "sharedlibrary") == 0) return AIDE_OUTPUT_SHARED_LIBRARY;
    return AIDE_OUTPUT_EXECUTABLE;
}

static const char *program_type_to_string(AideProgramType t)
{
    return t == AIDE_PROGRAM_WB ? "wb" : "cli";
}

static AideProgramType program_type_from_string(const char *s)
{
    return (s && strcmp(s, "wb") == 0) ? AIDE_PROGRAM_WB : AIDE_PROGRAM_CLI;
}

static const char *icon_mode_to_string(AideIconMode m)
{
    return m == AIDE_ICON_FILE ? "file" : "generate";
}

static AideIconMode icon_mode_from_string(const char *s)
{
    return (s && strcmp(s, "file") == 0) ? AIDE_ICON_FILE : AIDE_ICON_GENERATE;
}

static const char *startup_to_string(AideStartup s)
{
    switch (s) {
    case AIDE_STARTUP_MINSTART: return "minstart";
    case AIDE_STARTUP_MINRES: return "minres";
    case AIDE_STARTUP_CUSTOM: return "custom";
    case AIDE_STARTUP_STD: default: return "startup";
    }
}

static AideStartup startup_from_string(const char *s)
{
    if (!s)
        return AIDE_STARTUP_STD;
    if (strcmp(s, "minstart") == 0) return AIDE_STARTUP_MINSTART;
    if (strcmp(s, "minres") == 0) return AIDE_STARTUP_MINRES;
    if (strcmp(s, "custom") == 0) return AIDE_STARTUP_CUSTOM;
    return AIDE_STARTUP_STD;
}

/* ---- lifecycle ---- */

void aide_project_init(AideProject *p)
{
    memset(p, 0, sizeof(*p));

    p->name = aide_strdup("");

    p->output.binary = aide_strdup("");
    p->output.targetDir = aide_strdup("bin");
    p->output.objDir = aide_strdup("build/obj");

    p->language = AIDE_LANG_C;
    p->output.kind = AIDE_OUTPUT_EXECUTABLE;
    p->program.type = AIDE_PROGRAM_CLI;
    p->program.stackSize = 8192;
    p->program.iconMode = AIDE_ICON_GENERATE;
    p->program.iconPath = aide_strdup("");

    aide_strlist_init(&p->sources);
    aide_strlist_init(&p->headers);
    aide_strlist_init(&p->includePaths);
    aide_strlist_init(&p->defines);

    p->compiler.cpu = aide_strdup("68000");
    p->compiler.fpu = aide_strdup("none");
    p->compiler.optLevel = 2;
    p->compiler.smallData = 0;
    p->compiler.debugSymbols = 0;
    p->compiler.kick1hunks = 1;
    p->compiler.asmNoSym = 1;
    aide_strlist_init(&p->compiler.extraCFlags);
    aide_strlist_init(&p->compiler.extraAsmFlags);

    p->linker.startup = AIDE_STARTUP_STD;
    p->linker.customStartupPath = aide_strdup("");
    aide_strlist_init(&p->linker.libraries);
    aide_strlist_init(&p->linker.extraLibDirs);
    p->linker.strip = 0;
    aide_strlist_init(&p->linker.extraLdFlags);

    aide_strlist_init(&p->sdks.selected);
    aide_strlist_push(&p->sdks.selected, "ndk-3.2");

    p->run.copyToShared = 1;
    p->run.sharedSubdir = aide_strdup("");
    p->run.emulatorConfigOverride = aide_strdup("");
    aide_strlist_init(&p->run.extraFsUaeArgs);

    aide_strlist_init(&p->editor.openFiles);
    p->editor.activeFile = aide_strdup("");
}

void aide_project_free(AideProject *p)
{
    free(p->name);
    free(p->output.binary);
    free(p->output.targetDir);
    free(p->output.objDir);
    free(p->program.iconPath);
    aide_strlist_free(&p->sources);
    aide_strlist_free(&p->headers);
    aide_strlist_free(&p->includePaths);
    aide_strlist_free(&p->defines);
    free(p->compiler.cpu);
    free(p->compiler.fpu);
    aide_strlist_free(&p->compiler.extraCFlags);
    aide_strlist_free(&p->compiler.extraAsmFlags);
    free(p->linker.customStartupPath);
    aide_strlist_free(&p->linker.libraries);
    aide_strlist_free(&p->linker.extraLibDirs);
    aide_strlist_free(&p->linker.extraLdFlags);
    aide_strlist_free(&p->sdks.selected);
    free(p->run.sharedSubdir);
    free(p->run.emulatorConfigOverride);
    aide_strlist_free(&p->run.extraFsUaeArgs);
    aide_strlist_free(&p->editor.openFiles);
    free(p->editor.activeFile);
    memset(p, 0, sizeof(*p));
}

/* ---- JSON ---- */

static char *to_json(const AideProject *p, const char *raw_json_passthrough, int with_editor)
{
    JSON_Value *rootVal = json_value_init_object();
    JSON_Object *obj = json_value_get_object(rootVal);
    JSON_Value *sub;
    JSON_Object *subObj;
    size_t i;
    char *serialized;
    char *result;

    /* Preserve unknown top-level keys first, then overwrite known ones,
     * mirroring `QJsonObject obj = m_rawJson;` followed by field writes. */
    if (raw_json_passthrough && raw_json_passthrough[0]) {
        JSON_Value *rawVal = json_parse_string(raw_json_passthrough);
        if (rawVal && json_value_get_type(rawVal) == JSONObject) {
            JSON_Object *rawObj = json_value_get_object(rawVal);
            size_t n = json_object_get_count(rawObj);
            for (i = 0; i < n; ++i) {
                const char *key = json_object_get_name(rawObj, i);
                JSON_Value *copy;
                if (!with_editor && strcmp(key, "editor") == 0)
                    continue;   /* lives in <project>.aproj.user now */
                copy = json_value_deep_copy(json_object_get_value_at(rawObj, i));
                json_object_set_value(obj, key, copy);
            }
        }
        if (rawVal)
            json_value_free(rawVal);
    }

    json_object_set_string(obj, "format", "amigaide-project");
    json_object_set_number(obj, "version", 1);
    json_object_set_string(obj, "name", p->name);
    /* Written only for non-C projects: an .aproj produced before this key
     * existed must round-trip unchanged, and "c" is what its absence
     * already means. */
    if (p->language != AIDE_LANG_C)
        json_object_set_string(obj, "language", language_to_string(p->language));

    sub = json_value_init_object();
    subObj = json_value_get_object(sub);
    /* Written only when it is not the default, so an .aproj from before
     * this key existed round-trips unchanged. */
    if (p->output.kind != AIDE_OUTPUT_EXECUTABLE)
        json_object_set_string(subObj, "kind", output_kind_to_string(p->output.kind));
    json_object_set_string(subObj, "binary", p->output.binary);
    json_object_set_string(subObj, "targetDir", p->output.targetDir);
    json_object_set_string(subObj, "objDir", p->output.objDir);
    json_object_set_value(obj, "output", sub);

    sub = json_value_init_object();
    subObj = json_value_get_object(sub);
    json_object_set_string(subObj, "type", program_type_to_string(p->program.type));
    json_object_set_number(subObj, "stackSize", p->program.stackSize);
    {
        JSON_Value *iconVal = json_value_init_object();
        JSON_Object *iconObj = json_value_get_object(iconVal);
        json_object_set_string(iconObj, "mode", icon_mode_to_string(p->program.iconMode));
        if (p->program.iconPath[0])
            json_object_set_string(iconObj, "path", p->program.iconPath);
        else
            json_object_set_null(iconObj, "path");
        json_object_set_value(subObj, "icon", iconVal);
    }
    json_object_set_value(obj, "program", sub);

    {
        JSON_Value *srcArrVal = json_value_init_array();
        JSON_Array *srcArr = json_value_get_array(srcArrVal);
        for (i = 0; i < p->sources.count; ++i) {
            JSON_Value *srcObjVal = json_value_init_object();
            JSON_Object *srcObj = json_value_get_object(srcObjVal);
            json_object_set_string(srcObj, "path", p->sources.items[i]);
            json_array_append_value(srcArr, srcObjVal);
        }
        json_object_set_value(obj, "sources", srcArrVal);
    }

    json_object_set_value(obj, "headers", mk_strlist_array(&p->headers));
    json_object_set_value(obj, "includePaths", mk_strlist_array(&p->includePaths));
    json_object_set_value(obj, "defines", mk_strlist_array(&p->defines));

    sub = json_value_init_object();
    subObj = json_value_get_object(sub);
    json_object_set_string(subObj, "cpu", p->compiler.cpu);
    json_object_set_string(subObj, "fpu", p->compiler.fpu);
    json_object_set_number(subObj, "optLevel", p->compiler.optLevel);
    json_object_set_boolean(subObj, "smallData", p->compiler.smallData);
    json_object_set_boolean(subObj, "debugSymbols", p->compiler.debugSymbols);
    json_object_set_boolean(subObj, "kick1hunks", p->compiler.kick1hunks);
    json_object_set_boolean(subObj, "asmNoSym", p->compiler.asmNoSym);
    json_object_set_value(subObj, "extraCFlags", mk_strlist_array(&p->compiler.extraCFlags));
    json_object_set_value(subObj, "extraAsmFlags", mk_strlist_array(&p->compiler.extraAsmFlags));
    json_object_set_value(obj, "compiler", sub);

    sub = json_value_init_object();
    subObj = json_value_get_object(sub);
    json_object_set_string(subObj, "startup", startup_to_string(p->linker.startup));
    if (p->linker.customStartupPath[0])
        json_object_set_string(subObj, "customStartupPath", p->linker.customStartupPath);
    else
        json_object_set_null(subObj, "customStartupPath");
    json_object_set_value(subObj, "libraries", mk_strlist_array(&p->linker.libraries));
    json_object_set_value(subObj, "extraLibDirs", mk_strlist_array(&p->linker.extraLibDirs));
    json_object_set_boolean(subObj, "strip", p->linker.strip);
    json_object_set_value(subObj, "extraLdFlags", mk_strlist_array(&p->linker.extraLdFlags));
    json_object_set_value(obj, "linker", sub);

    sub = json_value_init_object();
    subObj = json_value_get_object(sub);
    json_object_set_value(subObj, "selected", mk_strlist_array(&p->sdks.selected));
    json_object_set_value(obj, "sdks", sub);

    sub = json_value_init_object();
    subObj = json_value_get_object(sub);
    json_object_set_boolean(subObj, "copyToShared", p->run.copyToShared);
    json_object_set_string(subObj, "sharedSubdir", p->run.sharedSubdir);
    if (p->run.emulatorConfigOverride[0])
        json_object_set_string(subObj, "emulatorConfigOverride", p->run.emulatorConfigOverride);
    else
        json_object_set_null(subObj, "emulatorConfigOverride");
    json_object_set_value(subObj, "extraFsUaeArgs", mk_strlist_array(&p->run.extraFsUaeArgs));
    json_object_set_value(obj, "run", sub);

    if (with_editor) {
        sub = json_value_init_object();
        subObj = json_value_get_object(sub);
        json_object_set_value(subObj, "openFiles", mk_strlist_array(&p->editor.openFiles));
        json_object_set_string(subObj, "activeFile", p->editor.activeFile);
        json_object_set_value(obj, "editor", sub);
    }

    serialized = json_serialize_to_string_pretty(rootVal);
    result = aide_strdup(serialized ? serialized : "");
    if (serialized)
        json_free_serialized_string(serialized);
    json_value_free(rootVal);
    return result;
}

char *aide_project_to_json(const AideProject *p, const char *raw_json_passthrough)
{
    return to_json(p, raw_json_passthrough, 1);
}

char *aide_project_to_file_json(const AideProject *p, const char *raw_json_passthrough)
{
    return to_json(p, raw_json_passthrough, 0);
}

void aide_project_user_path(const char *aproj_path, char *out, size_t outsz)
{
    if (!outsz) return;
    snprintf(out, outsz, "%s.user", aproj_path);
}

int aide_project_save_user(const AideProject *p, const char *aproj_path)
{
    JSON_Value *rootVal = json_value_init_object();
    JSON_Object *obj = json_value_get_object(rootVal);
    JSON_Value *sub = json_value_init_object();
    JSON_Object *subObj = json_value_get_object(sub);
    char path[1024];
    char *serialized;
    FILE *f;
    int ok = 0;

    json_object_set_value(subObj, "openFiles", mk_strlist_array(&p->editor.openFiles));
    json_object_set_string(subObj, "activeFile", p->editor.activeFile);
    json_object_set_value(obj, "editor", sub);
    serialized = json_serialize_to_string_pretty(rootVal);
    json_value_free(rootVal);
    if (!serialized)
        return 0;
    aide_project_user_path(aproj_path, path, sizeof(path));
    f = fopen(path, "wb");
    if (f) {
        size_t len = strlen(serialized);
        ok = fwrite(serialized, 1, len, f) == len;
        fclose(f);
    }
    json_free_serialized_string(serialized);
    return ok;
}

int aide_project_load_user(AideProject *p, const char *aproj_path)
{
    char path[1024];
    FILE *f;
    long size;
    char *buf;
    JSON_Value *val;
    JSON_Object *sub;

    aide_project_user_path(aproj_path, path, sizeof(path));
    f = fopen(path, "rb");
    if (!f)
        return 0;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0) { fclose(f); return 0; }
    buf = (char *)malloc((size_t)size + 1);
    if (!buf) { fclose(f); return 0; }
    buf[fread(buf, 1, (size_t)size, f)] = '\0';
    fclose(f);
    val = json_parse_string(buf);
    free(buf);
    if (!val || json_value_get_type(val) != JSONObject) {
        if (val) json_value_free(val);
        return 0;
    }
    sub = json_object_get_object(json_value_get_object(val), "editor");
    if (sub) {
        aide_strlist_free(&p->editor.openFiles);
        aide_strlist_init(&p->editor.openFiles);
        jstrlist(sub, "openFiles", &p->editor.openFiles);
        strset(&p->editor.activeFile, jstr(sub, "activeFile", ""));
    }
    json_value_free(val);
    return 1;
}

int aide_project_from_json(AideProject *p, const char *json_text)
{
    JSON_Value *rootVal = json_parse_string(json_text);
    JSON_Object *obj;
    JSON_Object *sub;

    if (!rootVal || json_value_get_type(rootVal) != JSONObject) {
        if (rootVal)
            json_value_free(rootVal);
        return 0;
    }
    obj = json_value_get_object(rootVal);

    strset(&p->name, jstr(obj, "name", ""));
    p->language = language_from_string(jstr(obj, "language", "c"));

    sub = json_object_get_object(obj, "output");
    p->output.kind = output_kind_from_string(jstr(sub, "kind", "executable"));
    strset(&p->output.binary, jstr(sub, "binary", p->name));
    strset(&p->output.targetDir, jstr(sub, "targetDir", "bin"));
    strset(&p->output.objDir, jstr(sub, "objDir", "build/obj"));

    sub = json_object_get_object(obj, "program");
    p->program.type = program_type_from_string(jstr(sub, "type", "cli"));
    p->program.stackSize = jint(sub, "stackSize", 8192);
    {
        JSON_Object *iconObj = sub ? json_object_get_object(sub, "icon") : NULL;
        p->program.iconMode = icon_mode_from_string(jstr(iconObj, "mode", "generate"));
        strset(&p->program.iconPath, jstr(iconObj, "path", ""));
    }

    aide_strlist_clear(&p->sources);
    {
        JSON_Array *arr = json_object_get_array(obj, "sources");
        if (arr) {
            size_t i, n = json_array_get_count(arr);
            for (i = 0; i < n; ++i) {
                JSON_Object *srcObj = json_array_get_object(arr, i);
                aide_strlist_push(&p->sources, jstr(srcObj, "path", ""));
            }
        }
    }

    jstrlist(obj, "headers", &p->headers);
    jstrlist(obj, "includePaths", &p->includePaths);
    jstrlist(obj, "defines", &p->defines);

    sub = json_object_get_object(obj, "compiler");
    strset(&p->compiler.cpu, jstr(sub, "cpu", "68000"));
    strset(&p->compiler.fpu, jstr(sub, "fpu", "none"));
    p->compiler.optLevel = jint(sub, "optLevel", 2);
    p->compiler.smallData = jbool(sub, "smallData", 0);
    p->compiler.debugSymbols = jbool(sub, "debugSymbols", 0);
    p->compiler.kick1hunks = jbool(sub, "kick1hunks", 1);
    p->compiler.asmNoSym = jbool(sub, "asmNoSym", 1);
    jstrlist(sub, "extraCFlags", &p->compiler.extraCFlags);
    jstrlist(sub, "extraAsmFlags", &p->compiler.extraAsmFlags);

    sub = json_object_get_object(obj, "linker");
    p->linker.startup = startup_from_string(jstr(sub, "startup", "startup"));
    strset(&p->linker.customStartupPath, jstr(sub, "customStartupPath", ""));
    jstrlist(sub, "libraries", &p->linker.libraries);
    jstrlist(sub, "extraLibDirs", &p->linker.extraLibDirs);
    p->linker.strip = jbool(sub, "strip", 0);
    jstrlist(sub, "extraLdFlags", &p->linker.extraLdFlags);

    sub = json_object_get_object(obj, "sdks");
    jstrlist(sub, "selected", &p->sdks.selected);

    sub = json_object_get_object(obj, "run");
    p->run.copyToShared = jbool(sub, "copyToShared", 1);
    strset(&p->run.sharedSubdir, jstr(sub, "sharedSubdir", ""));
    strset(&p->run.emulatorConfigOverride, jstr(sub, "emulatorConfigOverride", ""));
    jstrlist(sub, "extraFsUaeArgs", &p->run.extraFsUaeArgs);

    sub = json_object_get_object(obj, "editor");
    jstrlist(sub, "openFiles", &p->editor.openFiles);
    strset(&p->editor.activeFile, jstr(sub, "activeFile", ""));

    json_value_free(rootVal);
    return 1;
}

int aide_project_load(AideProject *p, const char *path)
{
    FILE *f;
    long size;
    char *buf;
    size_t readCount;
    int ok;

    f = fopen(path, "rb");
    if (!f)
        return 0;

    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0) {
        fclose(f);
        return 0;
    }

    buf = (char *)malloc((size_t)size + 1);
    readCount = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[readCount] = '\0';

    ok = aide_project_from_json(p, buf);
    free(buf);
    if (ok)
        aide_project_load_user(p, path);   /* open files etc., if any */
    return ok;
}

int aide_project_save(const AideProject *p, const char *path)
{
    char *json = aide_project_to_file_json(p, NULL);
    FILE *f = fopen(path, "wb");
    size_t len;
    size_t written;

    if (!f) {
        free(json);
        return 0;
    }

    len = strlen(json);
    written = fwrite(json, 1, len, f);
    fclose(f);
    free(json);
    if (written != len)
        return 0;
    aide_project_save_user(p, path);   /* a failure here only loses the open-files list */
    return 1;
}

void aide_project_create_minimal_c(AideProject *p, const char *name)
{
    aide_project_init(p);
    strset(&p->name, name);
    strset(&p->output.binary, name);
    aide_strlist_push(&p->sources, "src/main.c");
    p->linker.startup = AIDE_STARTUP_STD;
    aide_strlist_push(&p->linker.libraries, "vc");
    aide_strlist_push(&p->editor.openFiles, "src/main.c");
    strset(&p->editor.activeFile, "src/main.c");
}

/* ACE BASIC. No "vc" in linker.libraries and no startup selection: an ACE
 * program links against ACE's own startup.lib/db.lib, which aide_makegen
 * supplies from the language, not from linker config. */
void aide_project_create_minimal_basic(AideProject *p, const char *name)
{
    aide_project_init(p);
    p->language = AIDE_LANG_BASIC;
    strset(&p->name, name);
    strset(&p->output.binary, name);
    aide_strlist_push(&p->sources, "src/main.b");
    aide_strlist_push(&p->editor.openFiles, "src/main.b");
    strset(&p->editor.activeFile, "src/main.b");
}

void aide_project_create_minimal_asm(AideProject *p, const char *name)
{
    aide_project_init(p);
    strset(&p->name, name);
    strset(&p->output.binary, name);
    aide_strlist_push(&p->sources, "src/main.s");
    p->linker.startup = AIDE_STARTUP_MINSTART;
    aide_strlist_push(&p->linker.libraries, "vc");
    aide_strlist_push(&p->editor.openFiles, "src/main.s");
    strset(&p->editor.activeFile, "src/main.s");
}
