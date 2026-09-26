#include "SfdIndex.h"

// Thin adapter over the portable core/ implementation (core/src/aide_sfd.c,
// shared with the Amiga/MUI build).

SfdIndex::SfdIndex()
{
    aide_sfdindex_init(&m_index);
}

SfdIndex::~SfdIndex()
{
    aide_sfdindex_free(&m_index);
}

void SfdIndex::reload(const QStringList &sfdDirs)
{
    AideStrList dirs;
    aide_strlist_init(&dirs);
    for (const QString &d : sfdDirs) {
        const QByteArray utf8 = d.toUtf8();
        aide_strlist_push(&dirs, utf8.constData());
    }
    aide_sfdindex_reload(&m_index, &dirs);
    aide_strlist_free(&dirs);
}

const SfdFunction *SfdIndex::find(const QString &functionName) const
{
    const QByteArray nameUtf8 = functionName.toUtf8();
    const AideSfdFunction *fn = aide_sfdindex_find(&m_index, nameUtf8.constData());
    if (!fn)
        return nullptr;

    m_lastFound.name = QString::fromUtf8(fn->name);
    m_lastFound.library = QString::fromUtf8(fn->library);
    m_lastFound.rawSignature = QString::fromUtf8(fn->rawSignature);
    m_lastFound.registers = QString::fromUtf8(fn->registers);
    return &m_lastFound;
}

QStringList SfdIndex::allFunctionNames() const
{
    QStringList names;
    names.reserve(int(m_index.count));
    for (size_t i = 0; i < m_index.count; ++i)
        names << QString::fromUtf8(m_index.items[i].name);
    return names;
}
