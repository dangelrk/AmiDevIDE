#include "AppSettings.h"

#include "build/EmulatorRunner.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

namespace {
constexpr int kMaxRecentProjects = 10;

// Baked in by CMake from AMIGAIDE_TOOLCHAIN_ROOT. Only used when the IDE is
// running from a build tree, where there is no toolchain above it to find.
QString compiledInToolchainRoot()
{
#ifdef AMIGAIDE_DEFAULT_TOOLCHAIN_ROOT
    return QStringLiteral(AMIGAIDE_DEFAULT_TOOLCHAIN_ROOT);
#else
    return QDir::homePath() + QStringLiteral("/Dev/AmiDevIDE/toolchain");
#endif
}

QString defaultToolchainRoot()
{
    const QString detected = AppSettings::detectToolchainRoot();
    return detected.isEmpty() ? compiledInToolchainRoot() : detected;
}
}

bool AppSettings::looksLikeToolchain(const QString &dir)
{
    if (dir.isEmpty())
        return false;
    const QString vc = dir + QStringLiteral("/bin/vc");
    return QFileInfo::exists(vc) || QFileInfo::exists(vc + QStringLiteral(".exe"));
}

QString AppSettings::detectToolchainRoot()
{
    // amigaide is deployed into <toolchain>/bin, so walking up from the
    // executable finds the tree it belongs to. Four levels covers the macOS
    // bundle case, <toolchain>/bin/AmigaIDE.app/Contents/MacOS/amigaide.
    QDir dir(QCoreApplication::applicationDirPath());
    for (int up = 0; up < 5; ++up) {
        if (looksLikeToolchain(dir.absolutePath()))
            return dir.absolutePath();
        if (!dir.cdUp())
            break;
    }
    return {};
}

QStringList AppSettings::migrateStaleSettings()
{
    QSettings s;
    QStringList changes;

    const QString storedRoot = s.value(QStringLiteral("toolchain/root")).toString();
    const QString detected = detectToolchainRoot();

    if (!storedRoot.isEmpty() && !looksLikeToolchain(storedRoot)) {
        if (!detected.isEmpty()) {
            s.setValue(QStringLiteral("toolchain/root"), detected);
            changes << QObject::tr("Toolchain moved: %1 no longer exists, now using %2")
                           .arg(storedRoot, detected);
        } else {
            changes << QObject::tr("Toolchain not found at %1 — set it under "
                                   "Options ▸ Settings").arg(storedRoot);
        }
    }

    const QString root = toolchainRoot();

    // Paths derived from the old root are stale for the same reason, and a
    // stale shared directory is worse than a missing one: Run would silently
    // copy the binary somewhere the emulator does not mount.
    const QString shared = s.value(QStringLiteral("run/sharedDir")).toString();
    if (!shared.isEmpty() && !QDir(shared).exists()
        && (storedRoot.isEmpty() || shared.startsWith(storedRoot))) {
        s.remove(QStringLiteral("run/sharedDir"));
        changes << QObject::tr("Shared directory reset to %1/shared").arg(root);
    }

    // Also catches the retired per-OS layout, fs-uae/{mac,lin,win}/, which
    // is now a single generated fs-uae/development.fs-uae.
    const QString config = s.value(QStringLiteral("run/fsUaeConfig")).toString();
    if (!config.isEmpty() && !QFileInfo::exists(config)) {
        s.remove(QStringLiteral("run/fsUaeConfig"));
        changes << QObject::tr("FS-UAE configuration reset: %1 no longer exists")
                       .arg(config);
    }

    const QString emulator = s.value(QStringLiteral("run/fsUaeBinary")).toString();
    if (!emulator.isEmpty() && !QFileInfo::exists(emulator)) {
        s.remove(QStringLiteral("run/fsUaeBinary"));
        changes << QObject::tr("FS-UAE binary reset: %1 no longer exists").arg(emulator);
    }

    return changes;
}

AppSettings &AppSettings::instance()
{
    static AppSettings settings;
    return settings;
}

QString AppSettings::toolchainRoot() const
{
    QSettings s;
    return s.value(QStringLiteral("toolchain/root"), defaultToolchainRoot()).toString();
}

void AppSettings::setToolchainRoot(const QString &path)
{
    QSettings s;
    s.setValue(QStringLiteral("toolchain/root"), path);
}

QString AppSettings::makeProgram() const
{
    QSettings s;
    return s.value(QStringLiteral("toolchain/make"), QStringLiteral("make")).toString();
}

void AppSettings::setMakeProgram(const QString &path)
{
    QSettings s;
    s.setValue(QStringLiteral("toolchain/make"), path);
}

QString AppSettings::fsUaeBinary() const
{
    QSettings s;
    const QString stored = s.value(QStringLiteral("run/fsUaeBinary")).toString();
    if (!stored.isEmpty())
        return stored;
    // Probe rather than default to the macOS bundle path, which was simply
    // wrong on the other two platforms. Falls back to the bare name so PATH
    // still gets a chance if nothing is found.
    const QString found = EmulatorRunner::findEmulatorBinary();
    if (!found.isEmpty())
        return found;
    return QStringLiteral("fs-uae");
}

void AppSettings::setFsUaeBinary(const QString &path)
{
    QSettings s;
    s.setValue(QStringLiteral("run/fsUaeBinary"), path);
}

QString AppSettings::fsUaeConfig() const
{
    QSettings s;
    return s.value(QStringLiteral("run/fsUaeConfig")).toString();
}

void AppSettings::setFsUaeConfig(const QString &path)
{
    QSettings s;
    s.setValue(QStringLiteral("run/fsUaeConfig"), path);
}

QString AppSettings::sharedDir() const
{
    QSettings s;
    return s.value(QStringLiteral("run/sharedDir"),
                    toolchainRoot() + QStringLiteral("/shared"))
        .toString();
}

void AppSettings::setSharedDir(const QString &path)
{
    QSettings s;
    s.setValue(QStringLiteral("run/sharedDir"), path);
}

QString AppSettings::editorFontFamily() const
{
    QSettings s;
    return s.value(QStringLiteral("editor/fontFamily")).toString();
}

void AppSettings::setEditorFontFamily(const QString &family)
{
    QSettings s;
    s.setValue(QStringLiteral("editor/fontFamily"), family);
}

int AppSettings::editorFontPointSize() const
{
    QSettings s;
    return s.value(QStringLiteral("editor/fontPointSize"), 12).toInt();
}

void AppSettings::setEditorFontPointSize(int size)
{
    QSettings s;
    s.setValue(QStringLiteral("editor/fontPointSize"), size);
}

int AppSettings::editorTabWidth() const
{
    QSettings s;
    return s.value(QStringLiteral("editor/tabWidth"), 4).toInt();
}

void AppSettings::setEditorTabWidth(int chars)
{
    QSettings s;
    s.setValue(QStringLiteral("editor/tabWidth"), chars);
}

QString AppSettings::theme() const
{
    QSettings s;
    return s.value(QStringLiteral("editor/theme"), QStringLiteral("system")).toString();
}

void AppSettings::setTheme(const QString &theme)
{
    QSettings s;
    s.setValue(QStringLiteral("editor/theme"), theme);
}

QStringList AppSettings::recentProjects() const
{
    QSettings s;
    return s.value(QStringLiteral("recentProjects")).toStringList();
}

void AppSettings::addRecentProject(const QString &path)
{
    QSettings s;
    QStringList list = recentProjects();
    list.removeAll(path);
    list.prepend(path);
    while (list.size() > kMaxRecentProjects)
        list.removeLast();
    s.setValue(QStringLiteral("recentProjects"), list);
}
