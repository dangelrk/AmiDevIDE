#include "Project.h"
#include "ProjectAide.h"

#include "aide/aide_project.h"

#include <QDir>
#include <QFile>

#include <cstdlib>

// Thin adapter over the portable core/ implementation (core/src/aide_project.c,
// shared with the Amiga/MUI build).
// The .aproj JSON schema, field names and defaults live entirely in
// aide_project.c now; this file (plus ProjectAide.cpp's field-by-field
// conversion, reused by MakefileGenerator.cpp and friends) only bridges
// the Qt-facing QString/QList fields to the AideProject C struct.

QString Project::absolutePath(const QString &relative) const
{
    if (relative.isEmpty())
        return relative;
    QFileInfo info(relative);
    if (info.isAbsolute())
        return relative;
    return QDir(projectRoot()).absoluteFilePath(relative);
}

QStringList Project::allProjectFiles() const
{
    QStringList files;
    files.reserve(sources.size() + headers.size());
    for (const SourceFile &sf : sources)
        files << absolutePath(sf.path);
    for (const QString &header : headers)
        files << absolutePath(header);
    files.removeDuplicates();
    return files;
}

bool Project::load(const QString &path)
{
    AideProject a;
    aide_project_init(&a);

    const QByteArray pathUtf8 = path.toUtf8();
    if (!aide_project_load(&a, pathUtf8.constData())) {
        aide_project_free(&a);
        return false;
    }

    aideToProject(a, this);
    aide_project_free(&a);

    QFile file(path);
    if (file.open(QFile::ReadOnly))
        m_rawJsonText = QString::fromUtf8(file.readAll());

    m_filePath = path;
    return true;
}

bool Project::save(const QString &path)
{
    const QString target = path.isEmpty() ? m_filePath : path;
    if (target.isEmpty())
        return false;

    AideProject a;
    projectToAide(*this, &a);

    const QByteArray rawUtf8 = m_rawJsonText.toUtf8();
    // The .aproj without the editor state (open files change with every
    // session and would keep a versioned project modified); that goes into
    // <project>.auser, see aide_project_save_user().
    char *json = aide_project_to_file_json(&a, m_rawJsonText.isEmpty() ? nullptr : rawUtf8.constData());

    QFile file(target);
    if (!file.open(QFile::WriteOnly | QFile::Truncate)) {
        free(json);
        aide_project_free(&a);
        return false;
    }
    file.write(json);
    file.close();
    m_rawJsonText = QString::fromUtf8(json);
    free(json);
    aide_project_save_user(&a, target.toUtf8().constData());
    aide_project_free(&a);

    m_filePath = target;
    return true;
}

Project Project::createMinimalC(const QString &name)
{
    AideProject a;
    const QByteArray nameUtf8 = name.toUtf8();
    aide_project_create_minimal_c(&a, nameUtf8.constData());

    Project p;
    aideToProject(a, &p);
    aide_project_free(&a);
    return p;
}

Project Project::createMinimalBasic(const QString &name)
{
    AideProject a;
    const QByteArray nameUtf8 = name.toUtf8();
    aide_project_create_minimal_basic(&a, nameUtf8.constData());

    Project p;
    aideToProject(a, &p);
    aide_project_free(&a);
    return p;
}

Project Project::createMinimalAsm(const QString &name)
{
    AideProject a;
    const QByteArray nameUtf8 = name.toUtf8();
    aide_project_create_minimal_asm(&a, nameUtf8.constData());

    Project p;
    aideToProject(a, &p);
    aide_project_free(&a);
    return p;
}
