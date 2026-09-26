#include "HighlighterFactory.h"
#include "EditorWidget.h"
#include "EditorTheme.h"
#include "CHighlighter.h"
#include "AsmHighlighter.h"
#include "BasicHighlighter.h"
#include "app/AppSettings.h"

#include <QFileInfo>
#include <QString>

namespace HighlighterFactory {

void attach(EditorWidget *editor, const QString &filePath, ProjectLanguage language)
{
    const EditorTheme &theme = EditorTheme::resolve(AppSettings::instance().theme());
    const QString suffix = QFileInfo(filePath).suffix().toLower();
    const bool basicProject = language == ProjectLanguage::Basic;

    if (suffix == QLatin1String("b") || suffix == QLatin1String("bas")
        || (basicProject && suffix == QLatin1String("h"))) {
        editor->setHighlighter(new BasicHighlighter(editor->document(), theme));
    } else if (suffix == QLatin1String("c") || suffix == QLatin1String("h")) {
        editor->setHighlighter(new CHighlighter(editor->document(), theme));
    } else if (suffix == QLatin1String("s") || suffix == QLatin1String("asm") ||
               suffix == QLatin1String("i")) {
        editor->setHighlighter(new AsmHighlighter(editor->document(), theme));
    } else {
        editor->setHighlighter(nullptr);
    }
}

} // namespace HighlighterFactory
