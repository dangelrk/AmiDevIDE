#pragma once

#include <QDialog>

class QPlainTextEdit;

// Reads a Hunk file via HunkReader and renders each CODE section through
// M68kDisassembler: "hunk N (CODE):" header, then "$addr: mnemonic  operands"
// lines with symbol names inlined at their offsets.
class DisassemblyDialog : public QDialog {
    Q_OBJECT
public:
    explicit DisassemblyDialog(QWidget *parent = nullptr);

    // Loads and disassembles the file; shows an error message and returns
    // false if it isn't a readable Hunk file.
    bool disassemble(const QString &path);

private:
    QPlainTextEdit *m_output;
};
