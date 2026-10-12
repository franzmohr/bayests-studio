// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Franz X. Mohr

#include "views/ExtensionsDialog.h"

#include "engine/Examples.h"
#include "engine/Extensions.h"
#include "views/Palettes.h"

#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace studio
{

ExtensionsDialog::ExtensionsDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Extensions"));
    resize(760, 520);
    auto *layout = new QVBoxLayout(this);
    auto *intro = new QLabel(tr("Worked examples for the assistant and colour palettes, as JSON files. The studio "
                                "reads every .json file in your extensions folder and the studio-example.json of "
                                "every reproduction project. Write a template to start one: the README beside it "
                                "describes every field."));
    intro->setWordWrap(true);
    intro->setObjectName(QStringLiteral("subtle"));
    layout->addWidget(intro);

    folders_ = new QLabel;
    folders_->setWordWrap(true);
    folders_->setTextInteractionFlags(Qt::TextBrowserInteraction);
    folders_->setOpenExternalLinks(true);
    layout->addWidget(folders_);

    files_ = new QTreeWidget;
    files_->setColumnCount(2);
    files_->setHeaderLabels({tr("File"), tr("What it gave")});
    files_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    files_->header()->setStretchLastSection(true);
    files_->setToolTip(tr("Every extension file read, with the examples and palettes it gave; expand one for its "
                          "warnings, or for why it was not read."));
    layout->addWidget(files_, 1);
    summary_ = new QLabel;
    summary_->setWordWrap(true);
    layout->addWidget(summary_);

    auto *open = new QPushButton(tr("Open the Extensions Folder"));
    open->setToolTip(tr("Your extensions folder, created if it is not there yet."));
    connect(open, &QPushButton::clicked, this, []() {
        QDir().mkpath(userExtensionsFolder());
        QDesktopServices::openUrl(QUrl::fromLocalFile(userExtensionsFolder()));
    });
    auto *templ = new QPushButton(tr("Write a Template"));
    templ->setToolTip(tr("Writes README.md and template.json.txt into your extensions folder: one of the studio's "
                         "own examples and a palette, to copy into a .json file and edit."));
    connect(templ, &QPushButton::clicked, this, [this]() {
        try
        {
            const QString path = writeExtensionTemplate(userExtensionsFolder());
            QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
        }
        catch (const std::exception &e)
        {
            QMessageBox::warning(this, tr("Extensions"), QString::fromUtf8(e.what()));
        }
    });
    auto *again = new QPushButton(tr("Reload"));
    again->setToolTip(tr("Reads every extension file again; the assistant and the palettes take the result at once."));
    connect(again, &QPushButton::clicked, this, &ExtensionsDialog::reload);
    auto *row = new QHBoxLayout;
    row->addWidget(open);
    row->addWidget(templ);
    row->addStretch(1);
    row->addWidget(again);
    layout->addLayout(row);
    auto *box = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::accept);
    layout->addWidget(box);
    refresh();
}

void ExtensionsDialog::reload()
{
    loadExtensions(defaultExtensionFiles());
    const QStringList problems = reloadPalettes();
    refresh();
    if (!problems.isEmpty())
    {
        summary_->setText(summary_->text() + QStringLiteral("<br>") +
                          tr("Palettes left out: %1").arg(problems.join(QStringLiteral("; ")).toHtmlEscaped()));
    }
    emit reloaded();
}

void ExtensionsDialog::refresh()
{
    const QString reproductions =
        QSettings().value(QStringLiteral("assistant/reproductions"), defaultReproductionsFolder()).toString();
    const auto link = [](const QString &path) {
        return QStringLiteral("<a href=\"%1\">%2</a>")
            .arg(QUrl::fromLocalFile(path).toString(), QDir::toNativeSeparators(path).toHtmlEscaped());
    };
    folders_->setText(tr("Your extensions: %1<br>Reproduction projects: %2")
                          .arg(link(userExtensionsFolder()), link(reproductions)));

    files_->clear();
    int examples = 0, palettes = 0, failed = 0;
    for (const ExtensionReport &r : lastExtensionReports())
    {
        auto *item = new QTreeWidgetItem(files_);
        // Short: the name in the extensions folder, the project in the
        // reproductions; the whole path in the tooltip.
        const QFileInfo info(r.file);
        const bool reproduction = info.fileName() == QLatin1String("studio-example.json");
        item->setText(0, reproduction ? info.dir().dirName() + QStringLiteral("/") + info.fileName() : info.fileName());
        item->setToolTip(0, QDir::toNativeSeparators(r.file));
        if (!r.error.isEmpty())
        {
            ++failed;
            item->setText(1, tr("✗  not read: %1").arg(r.error));
            item->setForeground(1, QColor(0xB0, 0x30, 0x30));
            item->setToolTip(1, r.error);
            continue;
        }
        examples += static_cast<int>(r.examples.size());
        palettes += static_cast<int>(r.palettes.size());
        QStringList gave;
        if (!r.examples.isEmpty())
        {
            gave << tr("examples: %1").arg(r.examples.join(QStringLiteral(", ")));
        }
        if (!r.palettes.isEmpty())
        {
            gave << tr("palettes: %1").arg(r.palettes.join(QStringLiteral(", ")));
        }
        item->setText(1, (r.warnings.isEmpty() ? QStringLiteral("✓  ") : QStringLiteral("⚠  ")) +
                             (gave.isEmpty() ? tr("nothing") : gave.join(QStringLiteral("; "))));
        for (const QString &w : r.warnings)
        {
            auto *child = new QTreeWidgetItem(item);
            child->setText(1, w);
            child->setToolTip(1, w);
        }
        item->setExpanded(!r.warnings.isEmpty());
    }
    summary_->setText(lastExtensionReports().empty()
                          ? tr("No extension files yet.")
                          : tr("%1 file(s): %2 example(s) and %3 palette(s) read, %4 file(s) not read.")
                                .arg(lastExtensionReports().size())
                                .arg(examples)
                                .arg(palettes)
                                .arg(failed));
}

} // namespace studio
