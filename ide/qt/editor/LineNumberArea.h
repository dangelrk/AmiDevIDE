#pragma once

#include <QWidget>
#include <QSize>

class EditorWidget;

class LineNumberArea : public QWidget {
    Q_OBJECT
public:
    explicit LineNumberArea(EditorWidget *editor);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    EditorWidget *m_editor;
};
