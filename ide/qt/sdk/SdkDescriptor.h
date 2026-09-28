#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

enum class SdkKind { Ndk, Mui, Other };

// Describes one selectable SDK: its include/lib/autodoc/sfd locations. The
// built-in set is provided by SdkRegistry from the shipped sdk/ tree; users
// can add more without any code change (SdkEditDialog -> sdks.json).
struct SdkDescriptor {
    QString id;            // name + version, e.g. "ndk-3.2" (aide_sdk_make_id), used in .aproj
    QString displayName;   // e.g. "NDK 3.2"; for a user SDK just its name
    SdkKind kind = SdkKind::Other;
    QString version;
    QString rootDir;

    QStringList includeDirs;     // C -I paths
    QStringList asmIncludeDirs;  // vasm -I paths
    QStringList libDirs;         // vlink -L paths
    QStringList autodocDirs;     // directories scanned recursively for *.doc
    QStringList autodocFiles;    // single autodoc files (e.g. bsdsocket.doc)
    QStringList sfdDirs;         // directories containing *_lib.sfd
    QStringList suggestedLibs;   // library basenames to prefill in the linker tab

    bool userDefined = false;
    // Found through an AmiDevSDK.json under <toolchain>/sdk rather than
    // entered in SdkEditDialog: listed, but edited in that file, not here.
    bool fromManifest = false;

    // What the UI shows: the name, and the version unless it already says it.
    QString label() const;

    QJsonObject toJson() const;
    static SdkDescriptor fromJson(const QJsonObject &obj);
};
