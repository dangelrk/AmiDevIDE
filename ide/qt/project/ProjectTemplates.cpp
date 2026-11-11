#include "ProjectTemplates.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace ProjectTemplates {

namespace {

struct TemplateFile {
    const char *resource;   // path inside :/templates
    const char *target;     // path inside the project, relative to its root
    bool isSource;          // false -> written but not added to sources
};

// The shared-library starter set. testprog.c is written next to the
// library but deliberately NOT added to sources: it is a separate program
// that links against the finished library, not part of the library build.
const TemplateFile kSharedLibrary[] = {
    { ":/templates/shared-library/libstart.s", "src/libstart.s", true  },
    { ":/templates/shared-library/library.c",  "src/library.c",  true  },
    { ":/templates/shared-library/testprog.c", "testprog.c",     false },
};

bool writeResource(const QString &resource, const QString &absTarget)
{
    if (QFile::exists(absTarget))
        return false;
    if (!QDir().mkpath(QFileInfo(absTarget).absolutePath()))
        return false;

    QFile in(resource);
    if (!in.open(QFile::ReadOnly))
        return false;

    QFile out(absTarget);
    if (!out.open(QFile::WriteOnly))
        return false;

    out.write(in.readAll());
    return true;
}

} // namespace

QStringList apply(Project *project)
{
    QStringList created;

    if (!project || !project->sources.isEmpty())
        return created;
    if (project->output.kind != OutputKind::SharedLibrary)
        return created;

    for (const TemplateFile &tf : kSharedLibrary) {
        const QString rel = QString::fromLatin1(tf.target);
        if (!writeResource(QString::fromLatin1(tf.resource), project->absolutePath(rel)))
            continue;
        created << rel;
        if (tf.isSource)
            project->sources << SourceFile{ rel };
    }

    return created;
}

} // namespace ProjectTemplates
