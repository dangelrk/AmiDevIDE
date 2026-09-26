#include "aide/aide_diag.h"
#include "aide/aide_regex.h"
#include "aide/aide_str.h"

#include <stdlib.h>
#include <string.h>

/* vc/vbccm68k and vasmm68k_mot share this format:
 *   warning 161 in line 2 of "bad.c": implicit declaration of ...
 *   error 76 in line 2 of "bad2.c": identifier expected
 * groups: 1=severity 2=code(unused) 3=line 4=file 5=message */
static const char *COMPILER_PATTERN =
    "^[[:space:]]*(warning|error)[[:space:]]+([0-9]+)[[:space:]]+in line[[:space:]]+"
    "([0-9]+)[[:space:]]+of[[:space:]]+\"([^\"]+)\":[[:space:]]*(.*)$";

/* vlink: "Error 21: bad3.o (CODE+0x2): Reference to undefined symbol ..."
 * No source line number available; file is the object file, not source.
 * groups: 1=code(unused) 2=file 3=whole paren(unused) 4=paren content 5=message */
static const char *LINKER_PATTERN =
    "^[[:space:]]*Error[[:space:]]+([0-9]+):[[:space:]]*([^[:space:]]+)[[:space:]]*"
    "(\\(([^)]*)\\))?[[:space:]]*:[[:space:]]*(.*)$";

/* ace: "** <message> in line N." and, once yap -g markers have named a
 * file, "** <message> in line N of "FILE"." (ACE repo, lex.c
 * print_position). Unlike vc/vasm it prints no severity word and no error
 * number -- every one of these is an error. Without a file group the
 * issue stays unattached, exactly as the vlink branch below does.
 * groups: 1=message 2=line 4=file(optional) */
static const char *ACE_PATTERN =
    "^\\*\\*[[:space:]]*(.*[^[:space:]])[[:space:]]+in line[[:space:]]+([0-9]+)"
    "([[:space:]]+of[[:space:]]+\"([^\"]+)\")?\\.$";

static AideRegex *s_compiler_re = NULL;
static AideRegex *s_ace_re = NULL;
static AideRegex *s_linker_re = NULL;

static void ensure_compiled(void)
{
    if (!s_compiler_re)
        s_compiler_re = aide_regex_compile(COMPILER_PATTERN, AIDE_REGEX_ICASE);
    if (!s_linker_re)
        s_linker_re = aide_regex_compile(LINKER_PATTERN, 0);
    if (!s_ace_re)
        s_ace_re = aide_regex_compile(ACE_PATTERN, 0);
}

static char *group_dup(const char *line, const AideRegexMatch *m, int group)
{
    if (group >= m->group_count || m->start[group] < 0)
        return aide_strdup("");
    return aide_strndup(line + m->start[group], (size_t)(m->end[group] - m->start[group]));
}

static long group_len(const AideRegexMatch *m, int group)
{
    if (group >= m->group_count || m->start[group] < 0)
        return 0;
    return m->end[group] - m->start[group];
}

int aide_diag_parse_line(const char *line, AideIssue *out)
{
    AideRegexMatch m;

    ensure_compiled();

    if (aide_regex_exec(s_compiler_re, line, 0, &m)) {
        char *severity_word = group_dup(line, &m, 1);
        int is_error = aide_str_ieq(severity_word, "error");
        free(severity_word);

        out->severity = is_error ? AIDE_ISSUE_ERROR : AIDE_ISSUE_WARNING;
        out->file = group_dup(line, &m, 4);
        out->line = (int)strtol(line + m.start[3], NULL, 10);
        out->message = group_dup(line, &m, 5);
        return 1;
    }

    if (aide_regex_exec(s_ace_re, line, 0, &m)) {
        out->severity = AIDE_ISSUE_ERROR;
        out->file = group_dup(line, &m, 4);
        out->line = (int)strtol(line + m.start[2], NULL, 10);
        out->message = group_dup(line, &m, 1);
        return 1;
    }

    if (aide_regex_exec(s_linker_re, line, 0, &m)) {
        out->severity = AIDE_ISSUE_ERROR;
        out->file = group_dup(line, &m, 2);
        out->line = 0;

        if (group_len(&m, 4) == 0) {
            out->message = group_dup(line, &m, 5);
        } else {
            AideStr s;
            char *paren = group_dup(line, &m, 4);
            char *msg = group_dup(line, &m, 5);
            aide_str_init(&s);
            aide_str_append_char(&s, '(');
            aide_str_append(&s, paren);
            aide_str_append(&s, ") ");
            aide_str_append(&s, msg);
            free(paren);
            free(msg);
            out->message = aide_str_take(&s);
        }
        return 1;
    }

    return 0;
}
