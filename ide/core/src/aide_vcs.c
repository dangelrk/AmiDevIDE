/* aide_vcs.c -- see aide/aide_vcs.h */
#include "aide/aide_vcs.h"
#include "aide/aide_fs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* operations                                                          */

int aide_vcs_op_writes(AideVcsOp op)
{
    switch (op) {
    case AIDE_VCS_VERSION: case AIDE_VCS_STATUS: case AIDE_VCS_LOG:
    case AIDE_VCS_BRANCHES: case AIDE_VCS_PUSH:
        return 0;
    default:
        return 1;
    }
}

int aide_vcs_op_touches_files(AideVcsOp op)
{
    return op == AIDE_VCS_SWITCH || op == AIDE_VCS_MERGE || op == AIDE_VCS_CHECKOUT
        || op == AIDE_VCS_PULL || op == AIDE_VCS_CLONE;
}

int aide_vcs_op_network(AideVcsOp op)
{
    return op == AIDE_VCS_PUSH || op == AIDE_VCS_PULL || op == AIDE_VCS_CLONE;
}

/* ------------------------------------------------------------------ */
/* escaping                                                            */

char *aide_vcs_escape(const char *text)
{
    AideStr s;
    aide_str_init(&s);
    for (; text && *text; text++) {
        switch (*text) {
        case '\\': aide_str_append(&s, "\\\\"); break;
        case '\t': aide_str_append(&s, "\\t"); break;
        case '\n': aide_str_append(&s, "\\n"); break;
        case '\r': aide_str_append(&s, "\\r"); break;
        default:   aide_str_append_char(&s, *text);
        }
    }
    return aide_str_take(&s);
}

/* in place: \\ \t \n \r back to their characters */
static void unescape(char *s)
{
    char *o = s;
    for (; *s; s++) {
        if (*s == '\\' && s[1]) {
            s++;
            *o++ = *s == 't' ? '\t' : *s == 'n' ? '\n' : *s == 'r' ? '\r' : *s;
        } else {
            *o++ = *s;
        }
    }
    *o = '\0';
}

static int write_file(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");
    size_t n;
    if (!f) return 0;
    n = strlen(text);
    if (fwrite(text, 1, n, f) != n) { fclose(f); return 0; }
    return fclose(f) == 0;
}

/* ------------------------------------------------------------------ */
/* jobs                                                                */

int aide_vcs_job_build(AideVcsJob *job, AideVcsOp op, const char *program,
                       const char *tmpBase, const AideStrList *items,
                       const char *message, int flag, long logLimit)
{
    AideStr path;
    const char *cmd = "status";
    int wantArgs = 0;
    size_t i;

    memset(job, 0, sizeof(*job));
    job->op = op;
    aide_strlist_init(&job->argv);

    aide_str_init(&path);
    aide_str_appendf(&path, "%s.pc", tmpBase);
    job->resultPath = aide_str_take(&path);

    switch (op) {
    case AIDE_VCS_VERSION:    cmd = "version"; break;
    case AIDE_VCS_STATUS:     cmd = "status"; break;
    case AIDE_VCS_LOG:        cmd = "log"; break;
    case AIDE_VCS_BRANCHES:   cmd = "branch"; break;
    case AIDE_VCS_INIT:       cmd = "init"; break;
    case AIDE_VCS_ADD:        cmd = "add"; wantArgs = 1; break;
    case AIDE_VCS_UNSTAGE:    cmd = "unstage"; wantArgs = 1; break;
    case AIDE_VCS_COMMIT:     cmd = "commit"; break;
    case AIDE_VCS_BRANCH_NEW: cmd = "branch"; wantArgs = 1; break;
    case AIDE_VCS_SWITCH:     cmd = "switch"; wantArgs = 1; break;
    case AIDE_VCS_MERGE:      cmd = "merge"; wantArgs = 1; break;
    case AIDE_VCS_CHECKOUT:   cmd = "checkout"; break;
    case AIDE_VCS_PUSH:       cmd = "push"; break;
    case AIDE_VCS_PULL:       cmd = "pull"; wantArgs = items && items->count > 0; break;
    case AIDE_VCS_CLONE:      cmd = "clone"; wantArgs = 1; break;
    }

    aide_strlist_push(&job->argv, program);
    aide_strlist_push(&job->argv, "--porcelain-out");
    aide_strlist_push(&job->argv, job->resultPath);

    if (wantArgs && items && items->count > 0) {
        AideStr text;
        aide_str_init(&text);
        aide_str_init(&path);
        aide_str_appendf(&path, "%s.args", tmpBase);
        job->argsPath = aide_str_take(&path);
        for (i = 0; i < items->count; i++) {
            char *e = aide_vcs_escape(items->items[i]);
            aide_str_append(&text, e);
            aide_str_append_char(&text, '\n');
            free(e);
        }
        if (!write_file(job->argsPath, text.data)) { aide_str_free(&text); return 0; }
        aide_str_free(&text);
        aide_strlist_push(&job->argv, "--args-from");
        aide_strlist_push(&job->argv, job->argsPath);
    }

    aide_strlist_push(&job->argv, cmd);

    if (op == AIDE_VCS_LOG) {
        aide_strlist_push(&job->argv, "--all");
        if (logLimit > 0) {
            char n[24];
            sprintf(n, "%ld", logLimit);
            aide_strlist_push(&job->argv, "-n");
            aide_strlist_push(&job->argv, n);
        }
    } else if (op == AIDE_VCS_COMMIT) {
        aide_str_init(&path);
        aide_str_appendf(&path, "%s.msg", tmpBase);
        job->messagePath = aide_str_take(&path);
        if (!write_file(job->messagePath, message ? message : "")) return 0;
        aide_strlist_push(&job->argv, "-F");
        aide_strlist_push(&job->argv, job->messagePath);
        if (flag) aide_strlist_push(&job->argv, "-a");
    }
    return 1;
}

void aide_vcs_job_free(AideVcsJob *job)
{
    if (job->resultPath) remove(job->resultPath);
    if (job->argsPath) remove(job->argsPath);
    if (job->messagePath) remove(job->messagePath);
    free(job->resultPath);
    free(job->argsPath);
    free(job->messagePath);
    aide_strlist_free(&job->argv);
    memset(job, 0, sizeof(*job));
}

char *aide_vcs_job_cmdline(const AideVcsJob *job)
{
    AideStr s;
    size_t i;
    aide_str_init(&s);
    for (i = 0; i < job->argv.count; i++) {
        const char *a = job->argv.items[i];
        if (i) aide_str_append_char(&s, ' ');
        aide_str_append_char(&s, '"');
        for (; *a; a++) {
            if (*a == '"' || *a == '*') aide_str_append_char(&s, '*');
            aide_str_append_char(&s, *a);
        }
        aide_str_append_char(&s, '"');
    }
    return aide_str_take(&s);
}

/* ------------------------------------------------------------------ */
/* results                                                             */

void aide_vcs_result_init(AideVcsResult *r)
{
    memset(r, 0, sizeof(*r));
    r->rc = -1;
    aide_strlist_init(&r->refs);
}

void aide_vcs_result_free(AideVcsResult *r)
{
    size_t i;
    for (i = 0; i < r->changeCount; i++) free(r->changes[i].path);
    for (i = 0; i < r->commitCount; i++) {
        free(r->commits[i].oid); free(r->commits[i].parents); free(r->commits[i].author);
        free(r->commits[i].refs); free(r->commits[i].message);
    }
    for (i = 0; i < r->messageCount; i++) free(r->messages[i].text);
    free(r->changes);
    free(r->commits);
    free(r->messages);
    free(r->branch);
    free(r->committed);
    free(r->currentRef);
    free(r->progressLabel);
    aide_strlist_free(&r->refs);
    aide_vcs_result_init(r);
}

/* splits line (in place) at tabs into at most max fields, unescaping each */
static int split(char *line, char **f, int max)
{
    int n = 0;
    char *p = line;
    while (n < max) {
        char *tab = strchr(p, '\t');
        f[n++] = p;
        if (!tab) break;
        *tab = '\0';
        p = tab + 1;
    }
    {
        int k;
        for (k = 0; k < n; k++) unescape(f[k]);
    }
    return n;
}

static void *grow(void *items, size_t count, size_t size)
{
    /* grow by one; amortised enough for a few hundred records */
    return realloc(items, (count + 1) * size);
}

int aide_vcs_parse(AideVcsResult *r, const char *text)
{
    const char *p = text ? text : "";
    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        char *line = aide_strndup(p, len);
        char *f[8];
        int n;
        if (len && line[len - 1] == '\r') line[len - 1] = '\0';
        n = split(line, f, 8);
        if (strcmp(f[0], "format") == 0 && n >= 2) {
            r->format = atoi(f[1]);
        } else if (strcmp(f[0], "end") == 0 && n >= 2) {
            r->ended = 1;
            r->rc = atoi(f[1]);
        } else if ((strcmp(f[0], "branch") == 0 || strcmp(f[0], "detached") == 0) && n >= 2) {
            free(r->branch);
            r->branch = aide_strdup(f[1]);
            r->detached = f[0][0] == 'd';
            r->haveHead = 1;
        } else if (strcmp(f[0], "change") == 0 && n >= 4) {
            void *g = grow(r->changes, r->changeCount, sizeof(AideVcsChange));
            if (g) {
                r->changes = (AideVcsChange *)g;
                r->changes[r->changeCount].staged = f[1][0];
                r->changes[r->changeCount].kind = f[2][0];
                r->changes[r->changeCount].path = aide_strdup(f[3]);
                r->changeCount++;
            }
        } else if (strcmp(f[0], "ref") == 0 && n >= 3) {
            aide_strlist_push(&r->refs, f[2]);
            if (f[1][0] == '*') { free(r->currentRef); r->currentRef = aide_strdup(f[2]); }
        } else if (strcmp(f[0], "commit") == 0 && n >= 7) {
            void *g = grow(r->commits, r->commitCount, sizeof(AideVcsCommit));
            if (g) {
                AideVcsCommit *c;
                r->commits = (AideVcsCommit *)g;
                c = &r->commits[r->commitCount++];
                c->oid = aide_strdup(f[1]);
                c->parents = aide_strdup(f[2]);
                c->mtime = atol(f[3]);
                c->author = aide_strdup(f[4]);
                c->refs = aide_strdup(f[5]);
                c->message = aide_strdup(f[6]);
            }
        } else if (strcmp(f[0], "committed") == 0 && n >= 2) {
            free(r->committed);
            r->committed = aide_strdup(f[1]);
        } else if (strcmp(f[0], "msg") == 0 && n >= 3) {
            void *g = grow(r->messages, r->messageCount, sizeof(AideVcsMessage));
            if (g) {
                r->messages = (AideVcsMessage *)g;
                r->messages[r->messageCount].level =
                    strcmp(f[1], "error") == 0 ? 2 : strcmp(f[1], "warn") == 0 ? 1 : 0;
                r->messages[r->messageCount].text = aide_strdup(f[2]);
                r->messageCount++;
            }
        } else if (strcmp(f[0], "progress") == 0 && n >= 4) {
            r->progressDone = atol(f[1]);
            r->progressTotal = atol(f[2]);
            free(r->progressLabel);
            r->progressLabel = aide_strdup(f[3]);
        }
        /* anything else: a newer amivcs, skipped */
        free(line);
        if (!nl) break;
        p = nl + 1;
    }
    return r->format >= 1 && r->format <= AIDE_VCS_FORMAT;
}

int aide_vcs_parse_file(AideVcsResult *r, const char *path)
{
    char *data = NULL;
    size_t size = 0;
    int ok;
    if (!aide_fs_read_file(path, &data, &size)) return 0;
    ok = aide_vcs_parse(r, data);
    free(data);
    return ok;
}

const char *aide_vcs_result_error(const AideVcsResult *r)
{
    size_t i = r->messageCount;
    while (i > 0) {
        i--;
        if (r->messages[i].level == 2) return r->messages[i].text;
    }
    return NULL;
}

static int path_eq(const char *a, const char *b, int ignoreCase)
{
    return ignoreCase ? aide_str_ieq(a, b) : strcmp(a, b) == 0;
}

char aide_vcs_file_state(const AideVcsResult *status, const char *relPath, int ignoreCase)
{
    size_t i;
    char state = 0;
    if (!status || !relPath) return 0;
    if (relPath[0] == '.' && relPath[1] == '.') return 0;   /* outside the repository */
    for (i = 0; i < status->changeCount; i++) {
        const AideVcsChange *c = &status->changes[i];
        if (!path_eq(c->path, relPath, ignoreCase)) continue;
        if (c->staged == 'U') return c->kind;   /* unstaged changes win: there is work left */
        state = 'S';
    }
    return state;
}

int aide_vcs_present(const char *projectRoot)
{
    char *p = aide_fs_join(projectRoot, ".amivcs");
    int ok = p && aide_fs_exists(p);
    free(p);
    return ok;
}

int aide_vcs_has_theirs(const AideVcsResult *status)
{
    size_t i;
    for (i = 0; status && i < status->changeCount; i++) {
        size_t n = strlen(status->changes[i].path);
        if (n > 7 && strcmp(status->changes[i].path + n - 7, ".theirs") == 0) return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* .amivcsignore                                                       */

static int has_line(const char *text, const char *line)
{
    const char *p = text;
    size_t n = strlen(line);
    while (p && *p) {
        const char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        while (len > 0 && (p[len - 1] == '\r' || p[len - 1] == ' ')) len--;
        if (len == n && strncmp(p, line, n) == 0) return 1;
        if (!nl) break;
        p = nl + 1;
    }
    return 0;
}

static void want(AideStrList *out, const char *existing, const char *line)
{
    size_t i;
    if (!line || !line[0] || has_line(existing, line)) return;
    for (i = 0; i < out->count; i++) if (strcmp(out->items[i], line) == 0) return;
    aide_strlist_push(out, line);
}

void aide_vcs_ignore_missing(const AideProject *p, const char *existingText, AideStrList *out)
{
    AideStr s;
    const char *target = p->output.targetDir && p->output.targetDir[0] ? p->output.targetDir : "bin";
    const char *existing = existingText ? existingText : "";

    want(out, existing, "build/");               /* Makefile, vc.cfg, __stack.c, obj */
    aide_str_init(&s);
    aide_str_appendf(&s, "%s/", target);
    if (strcmp(s.data, "build/") != 0) want(out, existing, s.data);
    aide_str_free(&s);
    /* objDir outside build/ (only if configured elsewhere) */
    if (p->output.objDir && p->output.objDir[0] && strncmp(p->output.objDir, "build/", 6) != 0
        && strcmp(p->output.objDir, "build") != 0) {
        aide_str_init(&s);
        aide_str_appendf(&s, "%s/", p->output.objDir);
        want(out, existing, s.data);
        aide_str_free(&s);
    }
    want(out, existing, "*.aproj.user");
}

int aide_vcs_ignore_append(const char *projectRoot, const AideStrList *lines)
{
    char *path = aide_fs_join(projectRoot, ".amivcsignore");
    char *old = NULL;
    size_t oldSize = 0;
    AideStr text;
    size_t i;
    int ok;

    if (!path) return 0;
    aide_str_init(&text);
    if (aide_fs_read_file(path, &old, &oldSize) && old) {
        aide_str_append(&text, old);
        if (text.len && text.data[text.len - 1] != '\n') aide_str_append_char(&text, '\n');
        free(old);
    }
    aide_str_append(&text, "# AmigaIDE\n");
    for (i = 0; i < lines->count; i++) {
        aide_str_append(&text, lines->items[i]);
        aide_str_append_char(&text, '\n');
    }
    ok = write_file(path, text.data);
    aide_str_free(&text);
    free(path);
    return ok;
}
