#ifndef AIDE_ISSUE_H
#define AIDE_ISSUE_H

/* Portable counterpart of the Qt-side panes/Issue.h -- one build
 * diagnostic (compiler/assembler/linker) or a search/reference hit. */

#ifdef __cplusplus
extern "C" {
#endif

typedef enum AideIssueSeverity {
    AIDE_ISSUE_ERROR = 0,
    AIDE_ISSUE_WARNING = 1,
    AIDE_ISSUE_NOTE = 2,
} AideIssueSeverity;

typedef struct AideIssue {
    AideIssueSeverity severity;
    char *file;    /* malloc'd, owned; never NULL (points at ""  if unset) */
    int line;      /* 1-based; 0 if unknown (e.g. linker diagnostics) */
    char *message; /* malloc'd, owned; never NULL */
} AideIssue;

/* Frees file/message and resets the struct to a safe empty state.
 * Does not free `issue` itself (it is normally stack-allocated). */
void aide_issue_release(AideIssue *issue);

#ifdef __cplusplus
}
#endif

#endif /* AIDE_ISSUE_H */
