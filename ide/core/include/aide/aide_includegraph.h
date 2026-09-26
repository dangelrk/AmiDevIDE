#ifndef AIDE_INCLUDEGRAPH_H
#define AIDE_INCLUDEGRAPH_H

/* Portable C99 mirror of index/IncludeGraph.h -- tracks direct #include /
 * asm include-incdir edges, resolved to absolute paths, project-local
 * files only. See that header for the exact scope rationale. */

#include "aide/aide_str.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AideIncludeGraphEntry {
    char *file;         /* key: the including file's absolute path */
    AideStrList direct; /* its direct includes' absolute paths */
} AideIncludeGraphEntry;

typedef struct AideIncludeGraph {
    AideIncludeGraphEntry *entries;
    size_t count;
    size_t cap;
} AideIncludeGraph;

void aide_includegraph_init(AideIncludeGraph *g);
void aide_includegraph_free(AideIncludeGraph *g);

/* Replaces `file`'s direct-includes list (copies resolvedAbsoluteIncludes). */
void aide_includegraph_set_direct(AideIncludeGraph *g, const char *file, const AideStrList *resolvedAbsoluteIncludes);
void aide_includegraph_remove_file(AideIncludeGraph *g, const char *file);

/* Fills *out with `file`'s direct includes (cleared first); empty if `file`
 * isn't tracked. */
void aide_includegraph_direct_includes(const AideIncludeGraph *g, const char *file, AideStrList *out);
/* Fills *out with every file reachable from `file` via any number of
 * direct-include hops, each appearing once (order matches the Qt
 * original's stack-based DFS, not sorted). */
void aide_includegraph_transitive_includes(const AideIncludeGraph *g, const char *file, AideStrList *out);

/* Resolves a raw #include/incdir directive to an absolute path, if it
 * refers to an existing project-local file. Returns NULL otherwise (e.g.
 * SDK headers) -- freshly malloc'd on success; caller frees(). */
char *aide_includegraph_resolve(const char *directive, int quoted, const char *includingFileDir,
                                 const AideStrList *searchPaths);

#ifdef __cplusplus
}
#endif

#endif /* AIDE_INCLUDEGRAPH_H */
