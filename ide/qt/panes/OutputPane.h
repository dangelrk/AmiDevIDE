#pragma once

#include <QPlainTextEdit>
#include <QRegularExpression>

// Raw, read-only build/run output log. Recognizes "file:line" occurrences in
// appended text and turns them into clickable locations (BuildManager step).
class OutputPane : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit OutputPane(QWidget *parent = nullptr);

public slots:
    void appendLine(const QString &line);
    void clearOutput();

signals:
    void locationActivated(const QString &file, int line);

protected:
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    static QRegularExpression locationPattern();
};
