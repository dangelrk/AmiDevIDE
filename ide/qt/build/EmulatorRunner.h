#pragma once

#include <QObject>
#include <QString>

class Project;

// Copies the built binary (+ .info for WB programs) into the shared/
// directory and launches FS-UAE, per plan section 7. If FS-UAE already
// appears to be running, skips launching a second instance -- SHARED: is a
// live directory mount, so the new binary is immediately visible; the
// documented workflow is to restart the program from the Amiga shell.
class EmulatorRunner : public QObject {
    Q_OBJECT
public:
    explicit EmulatorRunner(QObject *parent = nullptr);

    void run(const Project &project);

    /* The FS-UAE executable this platform installs, or an empty string when
     * none is found. Used to seed the Settings dialog rather than hardcoding
     * a macOS bundle path as the default on every OS. */
    static QString findEmulatorBinary();

signals:
    void statusMessage(const QString &message);
    void errorOccurred(const QString &message);

private:
    bool isEmulatorRunning() const;
    QString defaultEmulatorConfigPath(const QString &toolchainRoot) const;
};
