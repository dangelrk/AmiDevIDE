#include "DisassemblyDialog.h"
#include "HunkReader.h"
#include "M68kDisassembler.h"

#include <QFileInfo>
#include <QFontDatabase>
#include <QHash>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QStringList>
#include <QVBoxLayout>

DisassemblyDialog::DisassemblyDialog(QWidget *parent)
    : QDialog(parent)
    , m_output(new QPlainTextEdit(this))
{
    setWindowTitle(tr("Disassembly"));
    resize(760, 640);

    m_output->setReadOnly(true);
    m_output->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_output->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_output);
}

bool DisassemblyDialog::disassemble(const QString &path)
{
    HunkReader reader;
    if (!reader.load(path)) {
        QMessageBox::warning(this, tr("Disassemble"),
                              tr("Could not read \"%1\":\n%2").arg(QFileInfo(path).fileName(), reader.errorString()));
        return false;
    }

    setWindowTitle(tr("Disassembly - %1").arg(QFileInfo(path).fileName()));

    QString out;
    for (const HunkSection &section : reader.sections()) {
        const char *kindName = section.kind == HunkSection::Kind::Code ? "CODE"
                              : section.kind == HunkSection::Kind::Data ? "DATA" : "BSS";
        out += QStringLiteral("; hunk %1 (%2), %3 bytes\n").arg(section.index).arg(kindName).arg(
            section.kind == HunkSection::Kind::Bss ? section.sizeBytes : quint32(section.data.size()));

        QHash<quint32, QStringList> symbolsAtOffset;
        for (const HunkSymbol &sym : section.symbols)
            symbolsAtOffset[sym.offset] << sym.name;

        if (section.kind != HunkSection::Kind::Code) {
            out += QStringLiteral("\n");
            continue;
        }

        int offset = 0;
        while (offset < section.data.size()) {
            if (symbolsAtOffset.contains(quint32(offset))) {
                for (const QString &name : symbolsAtOffset.value(quint32(offset)))
                    out += QStringLiteral("%1:\n").arg(name);
            }
            const DisassembledInstruction ins = M68kDisassembler::decodeOne(section.data, offset, 0);
            out += QStringLiteral("    %1  %2\t; %3\n")
                       .arg(ins.address, 6, 16, QChar('0'))
                       .arg(ins.rawHex.leftJustified(10))
                       .arg(ins.text);
        }
        out += QStringLiteral("\n");
    }

    m_output->setPlainText(out);
    return true;
}
