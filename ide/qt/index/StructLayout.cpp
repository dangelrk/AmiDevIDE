#include "StructLayout.h"

#include "aide/aide_structlayout.h"

#include <cstdlib>
#include <vector>

// Thin adapter over the portable core/ implementation (core/src/aide_structlayout.c,
// shared with the Amiga/MUI build).

namespace StructLayout {

Result compute(const QList<MemberDecl> &members, const QHash<QString, int> &knownStructSizes)
{
    std::vector<QByteArray> typeUtf8, nameUtf8;
    typeUtf8.reserve(members.size());
    nameUtf8.reserve(members.size());
    std::vector<AideMemberDecl> aideMembers;
    aideMembers.reserve(size_t(members.size()));
    for (const MemberDecl &m : members) {
        typeUtf8.push_back(m.type.toUtf8());
        nameUtf8.push_back(m.name.toUtf8());
    }
    for (int i = 0; i < members.size(); ++i) {
        AideMemberDecl d;
        d.type = const_cast<char *>(typeUtf8[size_t(i)].constData());
        d.name = const_cast<char *>(nameUtf8[size_t(i)].constData());
        aideMembers.push_back(d);
    }

    AideStrIntMap knownSizes;
    aide_strintmap_init(&knownSizes);
    for (auto it = knownStructSizes.constBegin(); it != knownStructSizes.constEnd(); ++it) {
        const QByteArray keyUtf8 = it.key().toUtf8();
        aide_strintmap_set(&knownSizes, keyUtf8.constData(), it.value());
    }

    AideStructLayoutResult aideResult;
    aide_structlayout_result_init(&aideResult);
    aide_structlayout_compute(aideMembers.empty() ? nullptr : aideMembers.data(), aideMembers.size(),
                               &knownSizes, &aideResult);

    Result result;
    result.totalSize = aideResult.totalSize;
    for (size_t i = 0; i < aideResult.memberCount; ++i) {
        MemberLayout layout;
        layout.name = QString::fromUtf8(aideResult.members[i].name);
        layout.type = QString::fromUtf8(aideResult.members[i].type);
        layout.offset = aideResult.members[i].offset;
        layout.size = aideResult.members[i].size;
        result.members << layout;
    }

    aide_structlayout_result_free(&aideResult);
    aide_strintmap_free(&knownSizes);
    return result;
}

} // namespace StructLayout
