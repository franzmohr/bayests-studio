// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Franz X. Mohr

#include "views/TableExport.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QShortcut>
#include <QTableWidget>
#include <QToolButton>
#include <QVariant>

namespace studio
{

namespace
{

QString suggestedFileName(const QString &caption)
{
    QString s;
    for (const QChar c : caption.toLower())
    {
        if (c.isLetterOrNumber() && c.unicode() < 128)
        {
            s += c;
        }
        else if (!s.isEmpty() && !s.endsWith(QLatin1Char('-')))
        {
            s += QLatin1Char('-');
        }
    }
    while (s.endsWith(QLatin1Char('-')))
    {
        s.chop(1);
    }
    return s.isEmpty() ? QStringLiteral("table") : s.left(60);
}

/// Writes `text` to a file the user picks; CSV gets a byte order mark, which
/// is what makes Excel read UTF-8.
void saveAs(QWidget *parent, const std::vector<TableContent> &tables, const QString &name)
{
    QString filter;
    const QString csv = QObject::tr("CSV (*.csv)");
    const QString tex = QObject::tr("LaTeX (*.tex)");
    const QString md = QObject::tr("Markdown (*.md)");
    const QString path = QFileDialog::getSaveFileName(parent, QObject::tr("Export Table"),
                                                      QDir::current().filePath(name + QStringLiteral(".tex")),
                                                      QStringList{tex, csv, md}.join(QStringLiteral(";;")), &filter);
    if (path.isEmpty())
    {
        return;
    }
    const QString suffix = QFileInfo(path).suffix().toLower();
    const bool asCsv = suffix == QLatin1String("csv") || (suffix.isEmpty() && filter == csv);
    const bool asMd = suffix == QLatin1String("md") || (suffix.isEmpty() && filter == md);
    QString text;
    for (std::size_t i = 0; i < tables.size(); ++i)
    {
        if (i > 0)
        {
            text += asCsv ? QStringLiteral("\r\n") : QStringLiteral("\n");
        }
        if (asCsv)
        {
            if (tables.size() > 1 && !tables[i].caption.isEmpty())
            {
                text += tables[i].caption + QStringLiteral("\r\n");
            }
            text += toCsv(tables[i]);
        }
        else if (asMd)
        {
            text += toMarkdown(tables[i]);
        }
        else
        {
            text += toLatex(tables[i]);
        }
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        QMessageBox::warning(parent, QObject::tr("Export Table"),
                             QObject::tr("Cannot write %1: %2").arg(path, file.errorString()));
        return;
    }
    if (asCsv)
    {
        file.write("\xEF\xBB\xBF");
    }
    file.write(text.toUtf8());
}

void copy(const TableContent &content)
{
    auto *mime = new QMimeData;
    mime->setText(toCsv(content, QLatin1Char('\t')));
    mime->setHtml(toHtml(content));
    QApplication::clipboard()->setMimeData(mime);
}

void fillMenu(QMenu *menu, QTableWidget *table)
{
    menu->addAction(QObject::tr("Copy"), table, [table]() { copy(tableContent(table)); });
    menu->addAction(QObject::tr("Copy as LaTeX"), table,
                    [table]() { QApplication::clipboard()->setText(toLatex(tableContent(table))); });
    menu->addAction(QObject::tr("Copy as Markdown"), table,
                    [table]() { QApplication::clipboard()->setText(toMarkdown(tableContent(table))); });
    menu->addSeparator();
    menu->addAction(QObject::tr("Save As…"), table, [table]() {
        const TableContent content = tableContent(table);
        saveAs(table->window(), {content}, suggestedFileName(content.caption));
    });
}

} // namespace

TableContent tableContent(const QTableWidget *table)
{
    TableContent out;
    out.caption = table->property("exportCaption").toString();
    out.notes = table->property("exportNotes").toString();
    for (const QVariant &v : table->property("exportRules").toList())
    {
        out.rulesBefore.push_back(v.toInt());
    }
    std::vector<int> columns;
    for (int c = 0; c < table->columnCount(); ++c)
    {
        if (!table->isColumnHidden(c))
        {
            columns.push_back(c);
            const QTableWidgetItem *h = table->horizontalHeaderItem(c);
            out.columnHeaders << (h ? h->text() : QString::number(c + 1));
        }
    }
    // Hidden, not invisible: a table on a tab not shown is invisible too.
    const bool rowHeaders = !table->verticalHeader()->isHidden();
    for (int r = 0; r < table->rowCount(); ++r)
    {
        if (table->isRowHidden(r))
        {
            continue;
        }
        if (rowHeaders)
        {
            const QTableWidgetItem *h = table->verticalHeaderItem(r);
            out.rowHeaders << (h ? h->text() : QString::number(r + 1));
        }
        QStringList row;
        std::vector<bool> bold, muted;
        for (const int c : columns)
        {
            const QTableWidgetItem *item = table->item(r, c);
            row << (item ? item->text() : QString());
            bold.push_back(item && item->font().bold());
            muted.push_back(item && item->data(TableMutedRole).toBool());
        }
        out.cells.push_back(row);
        out.bold.push_back(bold);
        out.muted.push_back(muted);
    }
    return out;
}

void setTableExportInfo(QTableWidget *table, const QString &caption, const QString &notes,
                        const std::vector<int> &rulesBefore)
{
    table->setProperty("exportCaption", caption);
    table->setProperty("exportNotes", notes);
    QVariantList rules;
    for (const int r : rulesBefore)
    {
        rules << r;
    }
    table->setProperty("exportRules", rules);
}

void attachTableExport(QTableWidget *table)
{
    table->setContextMenuPolicy(Qt::CustomContextMenu);
    QObject::connect(table, &QWidget::customContextMenuRequested, table, [table](const QPoint &at) {
        QMenu menu(table);
        fillMenu(&menu, table);
        menu.exec(table->viewport()->mapToGlobal(at));
    });
    auto *shortcut = new QShortcut(QKeySequence::Copy, table);
    shortcut->setContext(Qt::WidgetShortcut);
    QObject::connect(shortcut, &QShortcut::activated, table, [table]() { copy(tableContent(table)); });
}

QToolButton *tableExportButton(QTableWidget *table)
{
    auto *button = new QToolButton;
    button->setText(QObject::tr("Export"));
    button->setToolTip(QObject::tr("Copy the table, or save it as CSV, LaTeX or Markdown"));
    button->setPopupMode(QToolButton::InstantPopup);
    button->setObjectName(QStringLiteral("tableExport"));
    auto *menu = new QMenu(button);
    fillMenu(menu, table);
    button->setMenu(menu);
    return button;
}

void saveTables(QWidget *parent, const std::vector<TableContent> &tables, const QString &suggestedName)
{
    saveAs(parent, tables, suggestedName);
}

} // namespace studio
