#pragma once

#include <QString>
#include <QStringList>

// Thin QSettings wrapper for global (cross-project) IDE settings.
// M1: editor look & feel + toolchain root. M2 extends with FS-UAE paths,
// shared dir, recent projects, user-defined SDKs.
class AppSettings {
public:
    static AppSettings &instance();

    QString toolchainRoot() const;
    void setToolchainRoot(const QString &path);

    /* The toolchain tree this copy of the IDE lives in, worked out from the
     * executable's own location -- amigaide is deployed into <toolchain>/bin,
     * so the tree can be moved, renamed or copied to another machine and
     * still be found. Empty when the IDE is running from a build tree, where
     * the compile-time default applies instead. */
    static QString detectToolchainRoot();

    /* True if `dir` contains a usable toolchain (bin/vc). */
    static bool looksLikeToolchain(const QString &dir);

    /* Repairs settings left over from an earlier installation: a toolchain
     * root that no longer exists, and the paths derived from it. Returns one
     * human-readable line per change, empty when there was nothing to do.
     *
     * Needed because QSettings outlives the tree it describes. Moving the
     * toolchain -- or migrating from the old ~/Dev/amiga layout -- leaves a
     * stored path pointing at nothing, and the first sign of it is `make`
     * reporting "No such file or directory" for vc. */
    QStringList migrateStaleSettings();

    QString makeProgram() const;
    void setMakeProgram(const QString &path);

    /* AmiVCS (optional tool, like vc or ace): the amivcs program. Stored
     * as an absolute path -- a Finder-launched app has no shell PATH.
     * Default: <toolchain>/bin/amivcs, else amivcs from PATH, else "". */
    QString amivcsProgram() const;
    void setAmivcsProgram(const QString &path);
    /* The stand-alone AmiVCS GUI (amivcs-gui), started by Project >
     * Open AmiVCS GUI. Stored under "vcs/amivcsGui". Default: amivcs-gui
     * next to amivcs, then <toolchain>/bin, then PATH, else "". */
    QString amivcsGuiProgram() const;
    void setAmivcsGuiProgram(const QString &path);

    QString fsUaeBinary() const;
    void setFsUaeBinary(const QString &path);

    QString fsUaeConfig() const;
    void setFsUaeConfig(const QString &path);

    QString sharedDir() const;
    void setSharedDir(const QString &path);

    QString editorFontFamily() const;
    void setEditorFontFamily(const QString &family);

    int editorFontPointSize() const;
    void setEditorFontPointSize(int size);

    int editorTabWidth() const;
    void setEditorTabWidth(int chars);

    // "system", or one of EditorTheme::allThemes()'s ids (e.g. "one-dark").
    QString theme() const;
    void setTheme(const QString &theme);

    QStringList recentProjects() const;
    void addRecentProject(const QString &path);

private:
    AppSettings() = default;
};
