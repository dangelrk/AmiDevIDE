#pragma once

#include "project/Project.h"

class EditorWidget;
class QString;

// Attaches the correct QSyntaxHighlighter subclass to an EditorWidget based
// on the file extension: .c -> CHighlighter, .s/.asm/.i -> AsmHighlighter,
// .b/.bas -> BasicHighlighter.
//
// ".h" is ambiguous: ACE BASIC uses it for its headers too, and the
// preprocessor-only ones are textually indistinguishable from C. The
// project's language decides -- a project is either C or BASIC, never both.
namespace HighlighterFactory {
void attach(EditorWidget *editor, const QString &filePath,
            ProjectLanguage language = ProjectLanguage::C);
}
