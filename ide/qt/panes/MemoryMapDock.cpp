#include "MemoryMapDock.h"
#include "disasm/HunkReader.h"

#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {
const char *kindName(HunkSection::Kind kind)
{
    switch (kind) {
    case HunkSection::Kind::Code: return "CODE";
    case HunkSection::Kind::Data: return "DATA";
    case HunkSection::Kind::Bss:  return "BSS";
    }
    return "?";
}

quint32 sectionSize(const HunkSection &section)
{
    return section.kind == HunkSection::Kind::Bss ? section.sizeBytes : quint32(section.data.size());
}
}

MemoryMapDock::MemoryMapDock(QWidget *parent)
    : QWidget(parent)
    , m_tree(new QTreeWidget(this))
    , m_summaryLabel(new QLabel(this))
{
    m_tree->setColumnCount(3);
    m_tree->setHeaderLabels({ tr("Hunk"), tr("Kind"), tr("Size (bytes)") });
    m_tree->setRootIsDecorated(false);
    m_tree->setAlternatingRowColors(true);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);

    m_summaryLabel->setEnabled(false); // muted "status line" styling, matches SearchResultsDock's summary label

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_summaryLabel);
    layout->addWidget(m_tree);

    clear();
}

void MemoryMapDock::clear()
{
    m_tree->clear();
    m_summaryLabel->setText(tr("(no build yet)"));
}

void MemoryMapDock::showForBinary(const QString &path)
{
    m_tree->clear();

    HunkReader reader;
    if (!reader.load(path)) {
        m_summaryLabel->setText(tr("Could not read \"%1\": %2").arg(QFileInfo(path).fileName(), reader.errorString()));
        return;
    }

    quint32 loadSize = 0; // CODE + DATA: what actually has to be loaded into memory
    quint32 bssSize = 0;  // zero-filled at load time, not present in the file

    for (const HunkSection &section : reader.sections()) {
        const quint32 size = sectionSize(section);
        if (section.kind == HunkSection::Kind::Bss)
            bssSize += size;
        else
            loadSize += size;

        auto *item = new QTreeWidgetItem(m_tree);
        item->setText(0, tr("hunk %1").arg(section.index));
        item->setText(1, QString::fromLatin1(kindName(section.kind)));
        item->setText(2, QString::number(size));
        item->setTextAlignment(2, Qt::AlignRight | Qt::AlignVCenter);
    }

    m_summaryLabel->setText(tr("%1 -- load %2 bytes + %3 bytes BSS = %4 bytes total")
                                     .arg(QFileInfo(path).fileName())
                                     .arg(loadSize)
                                     .arg(bssSize)
                                     .arg(loadSize + bssSize));
}
