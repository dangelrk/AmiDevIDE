#pragma once

// Dialogs for AmiVCS remotes (AmiVCS 0.10: a remote URL and a login
// per repository, the author like git's user.name/user.email):
//
//   VcsLoginDialog     user, password, "remember" -- after amivcs
//                      answered a network job with auth-required
//   VcsSettingsDialog  remote URL, user, forget password, name/e-mail of
//                      the repository and the global defaults; reads and
//                      writes through "amivcs config", like every other
//                      amivcs operation (AmiVCS is never linked)

#include <QDialog>
#include <QMap>

class QCheckBox;
class QLineEdit;

class VcsLoginDialog : public QDialog {
    Q_OBJECT
public:
    VcsLoginDialog(const QString &url, const QString &user, bool refused, QWidget *parent = nullptr);
    QString user() const;
    QString password() const;
    bool remember() const;

private:
    QLineEdit *m_user;
    QLineEdit *m_pass;
    QCheckBox *m_remember;
};

class VcsSettingsDialog : public QDialog {
    Q_OBJECT
public:
    // program: the amivcs program; repoRoot: the repository (may be empty:
    // only the global defaults are shown)
    VcsSettingsDialog(const QString &program, const QString &repoRoot, const QString &intro = {},
                      QWidget *parent = nullptr);

private:
    void save();
    QMap<QString, QString> read(bool global) const;
    bool write(bool global, const QString &key, const QString &value, QString *error);

    QString m_program, m_root;
    QMap<QString, QString> m_repo, m_global;
    QLineEdit *m_remote = nullptr, *m_user = nullptr, *m_name = nullptr, *m_email = nullptr;
    QLineEdit *m_globalName = nullptr, *m_globalEmail = nullptr;
    QCheckBox *m_forget = nullptr;
};
