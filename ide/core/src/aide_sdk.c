#include "aide/aide_sdk.h"
#include "aide/aide_fs.h"

#include "parson.h"

#include <stdlib.h>
#include <string.h>

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

static const char *kind_to_string(AideSdkKind k)
{
    switch (k) {
    case AIDE_SDK_NDK: return "ndk";
    case AIDE_SDK_MUI: return "mui";
    case AIDE_SDK_OTHER: default: return "other";
    }
}

static AideSdkKind kind_from_string(const char *s)
{
    if (s && strcmp(s, "ndk") == 0) return AIDE_SDK_NDK;
    if (s && strcmp(s, "mui") == 0) return AIDE_SDK_MUI;
    return AIDE_SDK_OTHER;
}

/* ---- single descriptor ---- */

void aide_sdk_descriptor_init(AideSdkDescriptor *d)
{
    memset(d, 0, sizeof(*d));
    d->id = aide_strdup("");
    d->displayName = aide_strdup("");
    d->kind = AIDE_SDK_OTHER;
    d->version = aide_strdup("");
    d->rootDir = aide_strdup("");
    aide_strlist_init(&d->includeDirs);
    aide_strlist_init(&d->asmIncludeDirs);
    aide_strlist_init(&d->libDirs);
    aide_strlist_init(&d->autodocDirs);
    aide_strlist_init(&d->autodocFiles);
    aide_strlist_init(&d->sfdDirs);
    aide_strlist_init(&d->suggestedLibs);
    d->userDefined = 0;
}

void aide_sdk_descriptor_free(AideSdkDescriptor *d)
{
    free(d->id);
    free(d->displayName);
    free(d->version);
    free(d->rootDir);
    aide_strlist_free(&d->includeDirs);
    aide_strlist_free(&d->asmIncludeDirs);
    aide_strlist_free(&d->libDirs);
    aide_strlist_free(&d->autodocDirs);
    aide_strlist_free(&d->autodocFiles);
    aide_strlist_free(&d->sfdDirs);
    aide_strlist_free(&d->suggestedLibs);
    memset(d, 0, sizeof(*d));
}

static void strlist_copy(AideStrList *dst, const AideStrList *src)
{
    size_t i;
    aide_strlist_init(dst);
    for (i = 0; i < src->count; ++i)
        aide_strlist_push(dst, src->items[i]);
}

void aide_sdk_descriptor_copy(AideSdkDescriptor *dst, const AideSdkDescriptor *src)
{
    aide_sdk_descriptor_init(dst);
    strset(&dst->id, src->id);
    strset(&dst->displayName, src->displayName);
    dst->kind = src->kind;
    strset(&dst->version, src->version);
    strset(&dst->rootDir, src->rootDir);
    strlist_copy(&dst->includeDirs, &src->includeDirs);
    strlist_copy(&dst->asmIncludeDirs, &src->asmIncludeDirs);
    strlist_copy(&dst->libDirs, &src->libDirs);
    strlist_copy(&dst->autodocDirs, &src->autodocDirs);
    strlist_copy(&dst->autodocFiles, &src->autodocFiles);
    strlist_copy(&dst->sfdDirs, &src->sfdDirs);
    strlist_copy(&dst->suggestedLibs, &src->suggestedLibs);
    dst->userDefined = src->userDefined;
}

static JSON_Value *descriptor_to_json_value(const AideSdkDescriptor *d)
{
    JSON_Value *val = json_value_init_object();
    JSON_Object *obj = json_value_get_object(val);
    json_object_set_string(obj, "id", d->id);
    json_object_set_string(obj, "displayName", d->displayName);
    json_object_set_string(obj, "kind", kind_to_string(d->kind));
    json_object_set_string(obj, "version", d->version);
    json_object_set_string(obj, "rootDir", d->rootDir);
    json_object_set_value(obj, "includeDirs", mk_strlist_array(&d->includeDirs));
    json_object_set_value(obj, "asmIncludeDirs", mk_strlist_array(&d->asmIncludeDirs));
    json_object_set_value(obj, "libDirs", mk_strlist_array(&d->libDirs));
    json_object_set_value(obj, "autodocDirs", mk_strlist_array(&d->autodocDirs));
    json_object_set_value(obj, "autodocFiles", mk_strlist_array(&d->autodocFiles));
    json_object_set_value(obj, "sfdDirs", mk_strlist_array(&d->sfdDirs));
    json_object_set_value(obj, "suggestedLibs", mk_strlist_array(&d->suggestedLibs));
    return val;
}

static void descriptor_from_json_object(AideSdkDescriptor *d, const JSON_Object *obj)
{
    aide_sdk_descriptor_init(d);
    strset(&d->id, jstr(obj, "id", ""));
    strset(&d->displayName, jstr(obj, "displayName", ""));
    d->kind = kind_from_string(jstr(obj, "kind", "other"));
    strset(&d->version, jstr(obj, "version", ""));
    strset(&d->rootDir, jstr(obj, "rootDir", ""));
    jstrlist(obj, "includeDirs", &d->includeDirs);
    jstrlist(obj, "asmIncludeDirs", &d->asmIncludeDirs);
    jstrlist(obj, "libDirs", &d->libDirs);
    jstrlist(obj, "autodocDirs", &d->autodocDirs);
    jstrlist(obj, "autodocFiles", &d->autodocFiles);
    jstrlist(obj, "sfdDirs", &d->sfdDirs);
    jstrlist(obj, "suggestedLibs", &d->suggestedLibs);
    d->userDefined = 1;
}

char *aide_sdk_descriptor_to_json(const AideSdkDescriptor *d)
{
    JSON_Value *val = descriptor_to_json_value(d);
    char *serialized = json_serialize_to_string_pretty(val);
    char *result = aide_strdup(serialized ? serialized : "");
    if (serialized)
        json_free_serialized_string(serialized);
    json_value_free(val);
    return result;
}

int aide_sdk_descriptor_from_json(AideSdkDescriptor *d, const char *json_text)
{
    JSON_Value *root = json_parse_string(json_text);
    if (!root || json_value_get_type(root) != JSONObject) {
        if (root)
            json_value_free(root);
        return 0;
    }
    descriptor_from_json_object(d, json_value_get_object(root));
    json_value_free(root);
    return 1;
}

/* ---- list ---- */

void aide_sdk_list_init(AideSdkList *l)
{
    l->items = NULL;
    l->count = 0;
    l->cap = 0;
}

void aide_sdk_list_clear(AideSdkList *l)
{
    size_t i;
    for (i = 0; i < l->count; ++i)
        aide_sdk_descriptor_free(&l->items[i]);
    l->count = 0;
}

void aide_sdk_list_free(AideSdkList *l)
{
    aide_sdk_list_clear(l);
    free(l->items);
    l->items = NULL;
    l->cap = 0;
}

static void list_reserve(AideSdkList *l, size_t min_cap)
{
    size_t new_cap;
    AideSdkDescriptor *new_items;

    if (l->cap >= min_cap)
        return;
    new_cap = l->cap > 0 ? l->cap : 8;
    while (new_cap < min_cap)
        new_cap *= 2;

    new_items = (AideSdkDescriptor *)malloc(new_cap * sizeof(AideSdkDescriptor));
    memcpy(new_items, l->items, l->count * sizeof(AideSdkDescriptor));
    free(l->items);
    l->items = new_items;
    l->cap = new_cap;
}

void aide_sdk_list_push(AideSdkList *l, const AideSdkDescriptor *d)
{
    list_reserve(l, l->count + 1);
    aide_sdk_descriptor_copy(&l->items[l->count], d);
    l->count++;
}

int aide_sdk_list_remove(AideSdkList *l, const char *id)
{
    size_t i;
    for (i = 0; i < l->count; ++i) {
        if (aide_str_ieq(l->items[i].id, id)) {
            aide_sdk_descriptor_free(&l->items[i]);
            memmove(&l->items[i], &l->items[i + 1], (l->count - i - 1) * sizeof(AideSdkDescriptor));
            l->count--;
            return 1;
        }
    }
    return 0;
}

const AideSdkDescriptor *aide_sdk_list_find(const AideSdkList *l, const char *id)
{
    size_t i;
    for (i = 0; i < l->count; ++i) {
        if (aide_str_ieq(l->items[i].id, id))
            return &l->items[i];
    }
    return NULL;
}

/* ---- identity, self-describing SDKs, missing ids ---- */

static void append_id_part(AideStr *s, const char *part)
{
    int pendingDash = 0;
    for (; *part; ++part) {
        char c = *part;
        if (c >= 'A' && c <= 'Z')
            c = (char)(c - 'A' + 'a');
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.') {
            if (pendingDash && s->len > 0 && s->data[s->len - 1] != '-')
                aide_str_append_char(s, '-');
            pendingDash = 0;
            aide_str_append_char(s, c);
        } else {
            pendingDash = 1;
        }
    }
}

char *aide_sdk_make_id(const char *name, const char *version)
{
    AideStr s;
    aide_str_init(&s);
    append_id_part(&s, name ? name : "");
    if (s.len > 0 && version && version[0]) {
        size_t before = s.len;
        aide_str_append_char(&s, '-');
        append_id_part(&s, version);
        if (s.len == before + 1) { /* version had nothing usable */
            s.len = before;
            s.data[s.len] = '\0';
        }
    }
    return aide_str_take(&s);
}

static void manifest_paths(const JSON_Object *obj, const char *key, const char *rootDir, AideStrList *out)
{
    JSON_Array *arr = json_object_get_array(obj, key);
    size_t i, n;

    aide_strlist_clear(out);
    if (!arr)
        return;
    n = json_array_get_count(arr);
    for (i = 0; i < n; ++i) {
        const char *rel = json_array_get_string(arr, i);
        AideStr path;
        if (!rel || !rel[0])
            continue;
        aide_str_init(&path);
        aide_str_append(&path, rootDir);
        if (strcmp(rel, ".") != 0) {
            /* append_path knows that "SDK:" needs no '/' before a name */
            aide_str_append_path(&path, "/");
            aide_str_append(&path, rel);
        }
        aide_strlist_push(out, path.data);
        aide_str_free(&path);
    }
}

int aide_sdk_descriptor_from_manifest(AideSdkDescriptor *d, const char *json_text, const char *rootDir)
{
    JSON_Value *root = json_parse_string(json_text);
    const JSON_Object *obj;
    const char *name, *version;
    char *id;

    aide_sdk_descriptor_init(d);
    if (!root || json_value_get_type(root) != JSONObject) {
        if (root)
            json_value_free(root);
        return 0;
    }
    obj = json_value_get_object(root);
    name = jstr(obj, "name", "");
    version = jstr(obj, "version", "");
    id = aide_sdk_make_id(name, version);
    if (!name[0] || !version[0] || !id[0]) {
        free(id);
        json_value_free(root);
        return 0;
    }

    free(d->id);
    d->id = id;
    strset(&d->displayName, name);
    strset(&d->version, version);
    d->kind = kind_from_string(jstr(obj, "kind", "other"));
    strset(&d->rootDir, rootDir);
    manifest_paths(obj, "includeDirs", rootDir, &d->includeDirs);
    manifest_paths(obj, "asmIncludeDirs", rootDir, &d->asmIncludeDirs);
    manifest_paths(obj, "libDirs", rootDir, &d->libDirs);
    manifest_paths(obj, "autodocDirs", rootDir, &d->autodocDirs);
    manifest_paths(obj, "autodocFiles", rootDir, &d->autodocFiles);
    manifest_paths(obj, "sfdDirs", rootDir, &d->sfdDirs);
    jstrlist(obj, "suggestedLibs", &d->suggestedLibs);
    d->userDefined = 1;

    json_value_free(root);
    return 1;
}

size_t aide_sdk_append_from_manifests(const char *sdk_root, AideSdkList *out)
{
    AideStrList dirs;
    size_t i, added = 0;

    aide_strlist_init(&dirs);
    aide_fs_list_dirs(sdk_root, &dirs);
    for (i = 0; i < dirs.count; ++i) {
        AideStr dir, manifest;
        char *text;
        size_t size;
        AideSdkDescriptor d;

        aide_str_init(&dir);
        aide_str_append(&dir, sdk_root);
        aide_str_append_path(&dir, "/");
        aide_str_append(&dir, dirs.items[i]);

        aide_str_init(&manifest);
        aide_str_append(&manifest, dir.data);
        aide_str_append(&manifest, "/" AIDE_SDK_MANIFEST);

        if (aide_fs_read_file(manifest.data, &text, &size)) {
            if (aide_sdk_descriptor_from_manifest(&d, text, dir.data)
                && !aide_sdk_list_find(out, d.id)) {
                list_reserve(out, out->count + 1);
                out->items[out->count++] = d; /* ownership moves into the list */
                ++added;
            } else {
                aide_sdk_descriptor_free(&d);
            }
            free(text);
        }
        aide_str_free(&manifest);
        aide_str_free(&dir);
    }
    aide_strlist_free(&dirs);
    return added;
}

void aide_sdk_list_missing(const AideSdkList *registry, const AideStrList *selected, AideStrList *missing)
{
    size_t i;
    for (i = 0; i < selected->count; ++i) {
        if (!aide_sdk_list_find(registry, selected->items[i]))
            aide_strlist_push(missing, selected->items[i]);
    }
}

char *aide_sdk_list_to_json_array(const AideSdkList *l)
{
    JSON_Value *arrVal = json_value_init_array();
    JSON_Array *arr = json_value_get_array(arrVal);
    size_t i;
    char *serialized;
    char *result;

    for (i = 0; i < l->count; ++i)
        json_array_append_value(arr, descriptor_to_json_value(&l->items[i]));

    serialized = json_serialize_to_string_pretty(arrVal);
    result = aide_strdup(serialized ? serialized : "");
    if (serialized)
        json_free_serialized_string(serialized);
    json_value_free(arrVal);
    return result;
}

int aide_sdk_list_append_from_json_array(AideSdkList *out, const char *json_text)
{
    JSON_Value *root = json_parse_string(json_text);
    JSON_Array *arr;
    size_t i, n;

    if (!root || json_value_get_type(root) != JSONArray) {
        if (root)
            json_value_free(root);
        return 0;
    }
    arr = json_value_get_array(root);
    n = json_array_get_count(arr);
    for (i = 0; i < n; ++i) {
        JSON_Object *obj = json_array_get_object(arr, i);
        AideSdkDescriptor d;
        if (!obj)
            continue;
        descriptor_from_json_object(&d, obj);
        list_reserve(out, out->count + 1);
        out->items[out->count++] = d; /* transfer ownership directly, no extra copy */
    }
    json_value_free(root);
    return 1;
}

/* ---- built-in descriptor table (verbatim port of
 * SdkRegistry::builtinDescriptors) ---- */

static void add_builtin(AideSdkList *out, const char *id, const char *displayName,
                         AideSdkKind kind, const char *version, const char *rootDir)
{
    list_reserve(out, out->count + 1);
    aide_sdk_descriptor_init(&out->items[out->count]);
    strset(&out->items[out->count].id, id);
    strset(&out->items[out->count].displayName, displayName);
    out->items[out->count].kind = kind;
    strset(&out->items[out->count].version, version);
    strset(&out->items[out->count].rootDir, rootDir);
    out->count++;
}

static AideSdkDescriptor *last(AideSdkList *out)
{
    return &out->items[out->count - 1];
}

void aide_sdk_append_builtins(const char *sdk_root, AideSdkList *out)
{
    AideStr path;
    aide_str_init(&path);

    /* ndk-3.2 */
    aide_str_clear(&path);
    aide_str_append(&path, sdk_root);
    aide_str_append_path(&path, "/NDK_3.2");
    add_builtin(out, "ndk-3.2", "NDK 3.2", AIDE_SDK_NDK, "3.2", path.data);
    {
        AideSdkDescriptor *d = last(out);
        AideStr s;
        aide_str_init(&s); aide_str_append(&s, d->rootDir); aide_str_append(&s, "/Include_H");
        aide_strlist_push(&d->includeDirs, s.data); aide_str_free(&s);
        aide_str_init(&s); aide_str_append(&s, d->rootDir); aide_str_append(&s, "/Include_I");
        aide_strlist_push(&d->asmIncludeDirs, s.data); aide_str_free(&s);
        aide_str_init(&s); aide_str_append(&s, d->rootDir); aide_str_append(&s, "/lib");
        aide_strlist_push(&d->libDirs, s.data); aide_str_free(&s);
        aide_str_init(&s); aide_str_append(&s, d->rootDir); aide_str_append(&s, "/Autodocs");
        aide_strlist_push(&d->autodocDirs, s.data); aide_str_free(&s);
        aide_str_init(&s); aide_str_append(&s, d->rootDir); aide_str_append(&s, "/MuManual/Autodocs");
        aide_strlist_push(&d->autodocDirs, s.data); aide_str_free(&s);
        aide_str_init(&s); aide_str_append(&s, d->rootDir); aide_str_append(&s, "/SFD");
        aide_strlist_push(&d->sfdDirs, s.data); aide_str_free(&s);
    }

    /* mui-3.8 */
    aide_str_clear(&path);
    aide_str_append(&path, sdk_root);
    aide_str_append_path(&path, "/MUI_3.8");
    add_builtin(out, "mui-3.8", "MUI 3.8", AIDE_SDK_MUI, "3.8", path.data);
    {
        AideSdkDescriptor *d = last(out);
        AideStr s;
        aide_str_init(&s); aide_str_append(&s, d->rootDir); aide_str_append(&s, "/Developer/C/Include");
        aide_strlist_push(&d->includeDirs, s.data); aide_str_free(&s);
        aide_str_init(&s); aide_str_append(&s, d->rootDir); aide_str_append(&s, "/Developer/C/DLib");
        aide_strlist_push(&d->libDirs, s.data); aide_str_free(&s);
        aide_str_init(&s); aide_str_append(&s, d->rootDir); aide_str_append(&s, "/Developer/Autodocs");
        aide_strlist_push(&d->autodocDirs, s.data); aide_str_free(&s);
    }

    /* mcc -- third-party MUI classes (NList, NListtree, TextEditor, ...).
       The MUI frontend of the IDE links against these, so they belong in the
       registry rather than only in ide/amiga/Makefile.amiga-cross. */
    aide_str_clear(&path);
    aide_str_append(&path, sdk_root);
    aide_str_append_path(&path, "/MCC");
    add_builtin(out, "mcc", "MUI custom classes (MCC)", AIDE_SDK_MUI, "", path.data);
    {
        AideSdkDescriptor *d = last(out);
        AideStr s;
        aide_str_init(&s); aide_str_append(&s, d->rootDir); aide_str_append(&s, "/Developer/C/include");
        aide_strlist_push(&d->includeDirs, s.data); aide_str_free(&s);
        aide_str_init(&s); aide_str_append(&s, d->rootDir); aide_str_append(&s, "/Developer/Autodocs/MCC");
        aide_strlist_push(&d->autodocDirs, s.data); aide_str_free(&s);
    }

    /* regex -- regex.library, the Amiga backend of aide_regex */
    aide_str_clear(&path);
    aide_str_append(&path, sdk_root);
    aide_str_append_path(&path, "/regex");
    add_builtin(out, "regex", "regex.library", AIDE_SDK_OTHER, "", path.data);
    {
        AideSdkDescriptor *d = last(out);
        AideStr s;
        aide_str_init(&s); aide_str_append(&s, d->rootDir); aide_str_append(&s, "/include");
        aide_strlist_push(&d->includeDirs, s.data); aide_str_free(&s);
    }

    /* reqtools -- reqtools.library, used by the MUI frontend's requesters */
    aide_str_clear(&path);
    aide_str_append(&path, sdk_root);
    aide_str_append_path(&path, "/reqtools");
    add_builtin(out, "reqtools", "reqtools.library", AIDE_SDK_OTHER, "", path.data);
    {
        AideSdkDescriptor *d = last(out);
        AideStr s;
        aide_str_init(&s); aide_str_append(&s, d->rootDir); aide_str_append(&s, "/include");
        aide_strlist_push(&d->includeDirs, s.data); aide_str_free(&s);
    }

    /* roadshow */
    aide_str_clear(&path);
    aide_str_append(&path, sdk_root);
    aide_str_append_path(&path, "/Roadshow");
    add_builtin(out, "roadshow", "Roadshow (TCP/IP)", AIDE_SDK_OTHER, "", path.data);
    {
        AideSdkDescriptor *d = last(out);
        AideStr s;
        aide_str_init(&s); aide_str_append(&s, d->rootDir); aide_str_append(&s, "/include");
        aide_strlist_push(&d->includeDirs, s.data); aide_str_free(&s);
        aide_str_init(&s); aide_str_append(&s, d->rootDir); aide_str_append(&s, "/netinclude");
        aide_strlist_push(&d->includeDirs, s.data); aide_str_free(&s);
        aide_str_init(&s); aide_str_append(&s, d->rootDir); aide_str_append(&s, "/doc/bsdsocket.doc");
        aide_strlist_push(&d->autodocFiles, s.data); aide_str_free(&s);
    }

    /* amissl */
    aide_str_clear(&path);
    aide_str_append(&path, sdk_root);
    aide_str_append_path(&path, "/AmiSSL-v5");
    add_builtin(out, "amissl", "AmiSSL v5", AIDE_SDK_OTHER, "", path.data);
    {
        AideSdkDescriptor *d = last(out);
        AideStr s;
        aide_str_init(&s); aide_str_append(&s, d->rootDir); aide_str_append(&s, "/Developer/include");
        aide_strlist_push(&d->includeDirs, s.data); aide_str_free(&s);
        aide_str_init(&s); aide_str_append(&s, d->rootDir); aide_str_append(&s, "/Developer/lib/AmigaOS3"); /* GCC .a */
        aide_strlist_push(&d->libDirs, s.data); aide_str_free(&s);
        aide_str_init(&s); aide_str_append(&s, d->rootDir); aide_str_append(&s, "/Developer/Autodocs");
        aide_strlist_push(&d->autodocDirs, s.data); aide_str_free(&s);
    }

    /* sqlite3 */
    aide_str_clear(&path);
    aide_str_append(&path, sdk_root);
    aide_str_append_path(&path, "/SQLite3");
    add_builtin(out, "sqlite3", "SQLite3", AIDE_SDK_OTHER, "", path.data);
    {
        AideSdkDescriptor *d = last(out);
        AideStr s;
        aide_str_init(&s); aide_str_append(&s, d->rootDir); aide_str_append(&s, "/include");
        aide_strlist_push(&d->includeDirs, s.data); aide_str_free(&s);
        aide_str_init(&s); aide_str_append(&s, d->rootDir); aide_str_append(&s, "/lib"); /* GCC .a */
        aide_strlist_push(&d->libDirs, s.data); aide_str_free(&s);
    }

    /* posixlib */
    aide_str_clear(&path);
    aide_str_append(&path, sdk_root);
    aide_str_append_path(&path, "/PosixLib");
    add_builtin(out, "posixlib", "PosixLib", AIDE_SDK_OTHER, "", path.data);
    {
        AideSdkDescriptor *d = last(out);
        AideStr s;
        aide_str_init(&s); aide_str_append(&s, d->rootDir); aide_str_append(&s, "/include");
        aide_strlist_push(&d->includeDirs, s.data); aide_str_free(&s);
    }

    aide_str_free(&path);
}
