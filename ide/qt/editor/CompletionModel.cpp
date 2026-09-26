#include "CompletionModel.h"

#include "aide/aide_keywords.h"

// Thin adapter over the portable core/ implementation (core/src/aide_keywords.c,
// shared with the Amiga/MUI build).

namespace CompletionModel {

QStringList staticKeywords()
{
    static const QStringList keywords = [] {
        size_t count = 0;
        const char *const *words = aide_keywords_static(&count);
        QStringList list;
        list.reserve(int(count));
        for (size_t i = 0; i < count; ++i)
            list << QString::fromUtf8(words[i]);
        return list;
    }();
    return keywords;
}

} // namespace CompletionModel
