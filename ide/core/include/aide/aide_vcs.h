#ifndef AIDE_VCS_H
#define AIDE_VCS_H

/* Version control through AmiVCS, an optional external tool like vc or
 * ace: the IDE links nothing of AmiVCS and runs the program `amivcs` for
 * every operation, in the project directory. This module is the part both
 * frontends share (docs/AMIVCS-PLAN.md):
 *
 *   - building a job: the argument list and the files amivcs reads
 *     (--args-from with paths/branch names, commit -F with the message),
 *     so nothing on an AmigaDOS command line needs quoting;
 *   - reading the result: amivcs --porcelain-out writes one record per
 *     line (format, branch/detached, change, ref, commit, committed, msg,
 *     progress, auth-required, end), fields separated by tabs, escaped (\\ \t \n \r),
 *     all text in the local codeset;
 *   - file states for the project tree, the IDE's .amivcsignore lines.
 *
 * Running the job is the frontend's business (Qt: QProcess; MUI:
 * SystemTagList with NP_CurrentDir). The IDE never converts names:
 * amivcs speaks the local codeset on both sides.
 *
 * Portable C99, no Qt, no MUI. */

#include "aide/aide_str.h"
#include "aide/aide_project.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AIDE_VCS_FORMAT 1   /* the porcelain format this IDE understands */

typedef enum AideVcsOp {
    AIDE_VCS_VERSION,   /* format check */
    AIDE_VCS_STATUS,
    AIDE_VCS_LOG,       /* log --all -n <limit> */
    AIDE_VCS_BRANCHES,
    AIDE_VCS_INIT,
    AIDE_VCS_ADD,       /* paths */
    AIDE_VCS_UNSTAGE,   /* paths */
    AIDE_VCS_COMMIT,    /* message, flag = stage all */
    AIDE_VCS_BRANCH_NEW,/* one name */
    AIDE_VCS_SWITCH,    /* one name */
    AIDE_VCS_MERGE,     /* one name */
    AIDE_VCS_CHECKOUT,
    AIDE_VCS_PUSH,
    AIDE_VCS_PULL,
    AIDE_VCS_CLONE      /* remote URL, target directory */
} AideVcsOp;

/* 1 for an operation that changes the repository or the working tree:
 * it must not run beside a build/run or another writing job. */
int aide_vcs_op_writes(AideVcsOp op);
/* 1 for one that rewrites working-tree files (switch, merge, checkout,
 * pull, clone): editors must be saved before and reloaded after. */
int aide_vcs_op_touches_files(AideVcsOp op);
/* 1 for a network operation (push, pull, clone) */
int aide_vcs_op_network(AideVcsOp op);

typedef struct AideVcsJob {
    AideVcsOp op;
    AideStrList argv;        /* argv[0] = the amivcs program */
    char *resultPath;        /* --porcelain-out file */
    char *argsPath;          /* --args-from file, or NULL */
    char *messagePath;       /* commit -F file, or NULL */
} AideVcsJob;

/* Builds a job. `program`: the amivcs program; `tmpBase`: a path prefix
 * for the job's files (e.g. "T:aide-vcs" or "/tmp/aide-vcs-123"; the
 * module appends ".pc", ".args", ".msg"). `items`: paths (relative to the
 * project, local codeset) for add/unstage; the branch name for
 * branch/switch/merge/pull (optional for pull); the remote URL
 * (https://server/<name>) for push (optional: amivcs remembers it after
 * the first push or a clone); for clone the URL and the target directory.
 * Login data goes in front of the items, aide_vcs_items_with_login().
 * `message` for commit. `flag`: commit -a. `logLimit` for log (0 = all). Writes the args/message files.
 * Returns 1, or 0 if a file could not be written. */
int  aide_vcs_job_build(AideVcsJob *job, AideVcsOp op, const char *program,
                        const char *tmpBase, const AideStrList *items,
                        const char *message, int flag, long logLimit);
/* Removes the job's files and frees it. */
void aide_vcs_job_free(AideVcsJob *job);

/* One command line for AmigaDOS (SystemTagList): every argument in double
 * quotes, '"' and '*' escaped with '*' (the BCPL convention vbcc's
 * startup reads). Only the program path and fixed tmp file names end up
 * there anyway. malloc'd. */
char *aide_vcs_job_cmdline(const AideVcsJob *job);

/* ---- results ---- */

typedef struct AideVcsChange {
    char staged;          /* 'S' staged, 'U' not staged */
    char kind;            /* 'N' new, 'M' modified, 'D' deleted */
    char *path;           /* relative, local codeset, '/' separated */
} AideVcsChange;

typedef struct AideVcsCommit {
    char *oid;
    char *parents;        /* comma-separated oids, "" for the first commit */
    long  mtime;          /* UTC seconds; show as UTC */
    char *author;
    char *refs;           /* comma-separated branch names, "" if none */
    char *message;        /* may contain newlines */
} AideVcsCommit;

typedef struct AideVcsMessage {
    int   level;          /* 0 info, 1 warn, 2 error */
    char *text;
} AideVcsMessage;

typedef struct AideVcsResult {
    int   format;         /* 0 if the file had no format record */
    int   ended;          /* 1 if the end record was there */
    int   rc;             /* from the end record, -1 without one */
    int   haveHead;       /* a branch/detached record was there */
    int   detached;
    char *branch;         /* or the detached oid */
    char *committed;      /* oid of a new commit, or NULL */
    AideVcsChange *changes;  size_t changeCount;
    AideStrList    refs;     /* branch names */
    char          *currentRef; /* the one marked '*', or NULL */
    AideVcsCommit *commits;  size_t commitCount;
    AideVcsMessage *messages; size_t messageCount;
    long  progressDone, progressTotal;
    char *progressLabel;
    int   authRequired;   /* an auth-required record: ask and run again */
    char *authUrl;        /* the remote URL it was about */
    char *authUser;       /* the user amivcs had ("" if none) */
} AideVcsResult;

void aide_vcs_result_init(AideVcsResult *r);
void aide_vcs_result_free(AideVcsResult *r);
/* Parses porcelain text into *r (appends; unknown records are skipped).
 * Returns 1 if the text had a usable format (format >= 1 and <=
 * AIDE_VCS_FORMAT), 0 otherwise. */
int  aide_vcs_parse(AideVcsResult *r, const char *text);
/* Same for the result file of a job; 0 if it cannot be read. */
int  aide_vcs_parse_file(AideVcsResult *r, const char *path);

/* The newest error message (or NULL), for a requester. */
const char *aide_vcs_result_error(const AideVcsResult *r);

/* State of one project file from a status result: 'S' staged, 'M'/'N'/'D'
 * not staged, 0 if clean or not known. relPath relative to the project,
 * local codeset; compared case-insensitively when ignoreCase (Amiga). */
char aide_vcs_file_state(const AideVcsResult *status, const char *relPath, int ignoreCase);

/* 1 if <projectRoot>/.amivcs exists (the project is a repository) */
int  aide_vcs_present(const char *projectRoot);

/* Lines the IDE wants in .amivcsignore for this project: build/, the
 * target directory, the program and its .info if outside it, and
 * *.aproj.user. `existingText` is the current file (NULL if none);
 * only the missing lines are returned. */
void aide_vcs_ignore_missing(const AideProject *p, const char *existingText, AideStrList *out);
/* Appends lines to <projectRoot>/.amivcsignore. Returns 1 on success. */
int  aide_vcs_ignore_append(const char *projectRoot, const AideStrList *lines);

/* 1 if a failed push only lacked the remote URL (a repository created
 * locally, never pushed or cloned): ask for it and push again with the
 * URL as the item. */
int  aide_vcs_needs_remote_url(const AideVcsResult *r);

/* ---- remotes and login ---- */

/* 1 if `url` looks like a remote URL: scheme://host/<name> */
int  aide_vcs_is_url(const char *url);
/* The repository's name on the server, the last path element of a remote
 * URL ("https://host/a/proj/" -> "proj"); the default folder for a clone.
 * malloc'd, "" if there is none. */
char *aide_vcs_url_name(const char *url);
/* *out = --user/--password/--remember-password for amivcs (only the ones
 * given: user/pass NULL or "" are left out), followed by `items`. They
 * travel in the job's --args-from file, never on a command line; the file
 * is removed with the job. Retry a network job after an auth-required
 * result with these items. */
void aide_vcs_items_with_login(const AideStrList *items, const char *user, const char *pass,
                               int remember, AideStrList *out);

/* 1 if a status result lists `.theirs` copies (an unresolved merge) */
int  aide_vcs_has_theirs(const AideVcsResult *status);

/* Branch graph for a log result (commit records, newest first), same
 * lane layout as AmiVCS's own GUI: per row the commit's lane, the lanes
 * of the lines down to its parents, and every lane passing through. */
#define AIDE_VCS_MAX_PARENTS 8
typedef struct AideVcsGraphRow {
    int lane;
    int parentLanes[AIDE_VCS_MAX_PARENTS];
    int nParentLanes;
    int *activeLanes;     /* sorted ascending, includes `lane` */
    int nActiveLanes;
} AideVcsGraphRow;
/* rows: commitCount entries (malloc'd); maxLanes: columns to draw.
 * Returns 1, or 0 when out of memory. */
int  aide_vcs_graph(const AideVcsResult *log, AideVcsGraphRow **rows, int *maxLanes);
void aide_vcs_graph_free(AideVcsGraphRow *rows, size_t n);

/* Escapes one field the way amivcs reads --args-from lines. malloc'd. */
char *aide_vcs_escape(const char *text);

#ifdef __cplusplus
}
#endif

#endif /* AIDE_VCS_H */
