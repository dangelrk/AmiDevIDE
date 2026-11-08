#include "VcsDialogs.h"

#include "aide/aide_vcs.h"

#include <QCheckBox>
#include <QDir>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProcess>
#include <QVBoxLayout>

// ---- login -------------------------------------------------------------

VcsLoginDialog::VcsLoginDialog(const QString &url, const QString &user, bool refused, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("AmiVCS Login"));
    auto *intro = new QLabel(refused ? tr("<b>Login refused</b> -- wrong user or password?<br>%1").arg(url.toHtmlEscaped())
                                     : tr("Login for %1").arg(url.toHtmlEscaped()), this);
    intro->setWordWrap(true);
    m_user = new QLineEdit(user, this);
    m_pass = new QLineEdit(this);
    m_pass->setEchoMode(QLineEdit::Password);
    m_remember = new QCheckBox(tr("Remember the password (plain text in .amivcs/config)"), this);
    auto *form = new QFormLayout;
    form->addRow(tr("User:"), m_user);
    form->addRow(tr("Password:"), m_pass);
    form->addRow(QString(), m_remember);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto *lay = new QVBoxLayout(this);
    lay->addWidget(intro);
    lay->addLayout(form);
    lay->addWidget(buttons);
    (user.isEmpty() ? m_user : m_pass)->setFocus();
}

QString VcsLoginDialog::user() const { return m_user->text().trimmed(); }
QString VcsLoginDialog::password() const { return m_pass->text(); }
bool VcsLoginDialog::remember() const { return m_remember->isChecked(); }

// ---- settings ----------------------------------------------------------

VcsSettingsDialog::VcsSettingsDialog(const QString &program, const QString &repoRoot, const QString &intro,
                                     QWidget *parent)
    : QDialog(parent), m_program(program), m_root(repoRoot)
{
    setWindowTitle(tr("AmiVCS Settings"));
    auto *lay = new QVBoxLayout(this);
    if (!intro.isEmpty()) {
        auto *l = new QLabel(intro, this);
        l->setWordWrap(true);
        lay->addWidget(l);
    }
    m_global = read(true);
    if (!m_root.isEmpty()) {
        m_repo = read(false);
        auto *box = new QGroupBox(tr("This repository"), this);
        auto *form = new QFormLayout(box);
        m_remote = new QLineEdit(m_repo.value(QStringLiteral("remote")), box);
        m_remote->setPlaceholderText(QStringLiteral("https://server/name"));
        m_user = new QLineEdit(m_repo.value(QStringLiteral("user")), box);
        m_forget = new QCheckBox(tr("Forget the remembered password"), box);
        m_forget->setEnabled(m_repo.contains(QStringLiteral("pass")));
        m_name = new QLineEdit(m_repo.value(QStringLiteral("name")), box);
        m_name->setPlaceholderText(tr("empty: the default below"));
        m_email = new QLineEdit(m_repo.value(QStringLiteral("email")), box);
        m_email->setPlaceholderText(tr("empty: the default below"));
        form->addRow(tr("Remote URL:"), m_remote);
        form->addRow(tr("User:"), m_user);
        form->addRow(QString(), m_forget);
        form->addRow(tr("Name:"), m_name);
        form->addRow(tr("E-mail:"), m_email);
        lay->addWidget(box);
    }
    auto *gbox = new QGroupBox(tr("Defaults for every repository"), this);
    auto *gform = new QFormLayout(gbox);
    m_globalName = new QLineEdit(m_global.value(QStringLiteral("name")), gbox);
    m_globalEmail = new QLineEdit(m_global.value(QStringLiteral("email")), gbox);
    gform->addRow(tr("Name:"), m_globalName);
    gform->addRow(tr("E-mail:"), m_globalEmail);
    lay->addWidget(gbox);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &VcsSettingsDialog::save);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(buttons);
    resize(qMax(width(), 480), height());
}

// "amivcs config [--global]" prints key=value lines (pass as ********)
QMap<QString, QString> VcsSettingsDialog::read(bool global) const
{
    QMap<QString, QString> map;
    QProcess p;
    p.setWorkingDirectory(m_root.isEmpty() ? QDir::tempPath() : m_root);
    p.start(m_program, global ? QStringList{ QStringLiteral("config"), QStringLiteral("--global") }
                              : QStringList{ QStringLiteral("config") });
    if (!p.waitForFinished(10000) || p.exitCode() != 0) return map;
    const QStringList lines = QString::fromUtf8(p.readAllStandardOutput()).split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq > 0) map.insert(line.left(eq).trimmed(), line.mid(eq + 1).trimmed());
    }
    return map;
}

bool VcsSettingsDialog::write(bool global, const QString &key, const QString &value, QString *error)
{
    const QMap<QString, QString> &old = global ? m_global : m_repo;
    if (old.value(key) == value) return true;
    QStringList args{ QStringLiteral("config") };
    if (global) args << QStringLiteral("--global");
    if (value.isEmpty()) args << QStringLiteral("--unset") << key;
    else args << key << value;
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.setWorkingDirectory(m_root.isEmpty() ? QDir::tempPath() : m_root);
    p.start(m_program, args);
    if (p.waitForFinished(10000) && p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0) return true;
    *error = QString::fromUtf8(p.readAll()).trimmed();
    if (error->isEmpty()) *error = tr("%1 could not be run.").arg(m_program);
    return false;
}

void VcsSettingsDialog::save()
{
    if (m_remote && !m_remote->text().trimmed().isEmpty()
        && !aide_vcs_is_url(m_remote->text().trimmed().toUtf8().constData())) {
        QMessageBox::warning(this, windowTitle(), tr("'%1' is not a repository URL (https://server/name).")
                                                      .arg(m_remote->text().trimmed()));
        return;
    }
    QString err;
    bool ok = true;
    if (m_remote) {
        ok = ok && write(false, QStringLiteral("remote"), m_remote->text().trimmed(), &err);
        ok = ok && write(false, QStringLiteral("user"), m_user->text().trimmed(), &err);
        ok = ok && write(false, QStringLiteral("name"), m_name->text().trimmed(), &err);
        ok = ok && write(false, QStringLiteral("email"), m_email->text().trimmed(), &err);
        if (ok && m_forget->isChecked()) ok = write(false, QStringLiteral("pass"), QString(), &err);
    }
    ok = ok && write(true, QStringLiteral("name"), m_globalName->text().trimmed(), &err);
    ok = ok && write(true, QStringLiteral("email"), m_globalEmail->text().trimmed(), &err);
    if (!ok) {
        QMessageBox::warning(this, windowTitle(), err);
        return;
    }
    accept();
}
