#include "IncludeGraph.h"

#include <cstdlib>

// Thin adapter over the portable core/ implementation (core/src/aide_includegraph.c,
// shared with the Amiga/MUI build).
// File-existence/path-resolution now goes through core/include/aide/aide_fs.h,
// backed by core/platform/aide_fs_posix.c on this build.

namespace {

AideStrList toAideStrList(const QStringList &in)
{
    AideStrList out;
    aide_strlist_init(&out);
    for (const QString &s : in) {
        const QByteArray utf8 = s.toUtf8();
        aide_strlist_push(&out, utf8.constData());
    }
    return out;
}

QStringList toQStringList(const AideStrList &in)
{
    QStringList out;
    out.reserve(int(in.count));
    for (size_t i = 0; i < in.count; ++i)
        out << QString::fromUtf8(in.items[i]);
    return out;
}

} // namespace

IncludeGraph::IncludeGraph()
{
    aide_includegraph_init(&m_graph);
}

IncludeGraph::~IncludeGraph()
{
    aide_includegraph_free(&m_graph);
}

void IncludeGraph::setDirectIncludes(const QString &file, const QStringList &resolvedAbsoluteIncludes)
{
    const QByteArray fileUtf8 = file.toUtf8();
    AideStrList list = toAideStrList(resolvedAbsoluteIncludes);
    aide_includegraph_set_direct(&m_graph, fileUtf8.constData(), &list);
    aide_strlist_free(&list);
}

void IncludeGraph::removeFile(const QString &file)
{
    const QByteArray fileUtf8 = file.toUtf8();
    aide_includegraph_remove_file(&m_graph, fileUtf8.constData());
}

QStringList IncludeGraph::directIncludes(const QString &file) const
{
    const QByteArray fileUtf8 = file.toUtf8();
    AideStrList out;
    aide_strlist_init(&out);
    aide_includegraph_direct_includes(&m_graph, fileUtf8.constData(), &out);
    const QStringList result = toQStringList(out);
    aide_strlist_free(&out);
    return result;
}

QStringList IncludeGraph::transitiveIncludes(const QString &file) const
{
    const QByteArray fileUtf8 = file.toUtf8();
    AideStrList out;
    aide_strlist_init(&out);
    aide_includegraph_transitive_includes(&m_graph, fileUtf8.constData(), &out);
    const QStringList result = toQStringList(out);
    aide_strlist_free(&out);
    return result;
}

QString IncludeGraph::resolveInclude(const QString &directive, bool quoted,
                                      const QString &includingFileDir,
                                      const QStringList &searchPaths)
{
    const QByteArray directiveUtf8 = directive.toUtf8();
    const QByteArray dirUtf8 = includingFileDir.toUtf8();
    AideStrList paths = toAideStrList(searchPaths);

    char *resolved = aide_includegraph_resolve(directiveUtf8.constData(), quoted ? 1 : 0,
                                                dirUtf8.constData(), &paths);
    aide_strlist_free(&paths);

    if (!resolved)
        return QString();
    const QString result = QString::fromUtf8(resolved);
    free(resolved);
    return result;
}
