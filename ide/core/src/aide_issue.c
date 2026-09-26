#include "aide/aide_issue.h"

#include <stdlib.h>

void aide_issue_release(AideIssue *issue)
{
    free(issue->file);
    free(issue->message);
    issue->file = NULL;
    issue->message = NULL;
    issue->line = 0;
    issue->severity = AIDE_ISSUE_ERROR;
}
