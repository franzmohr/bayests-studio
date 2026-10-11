#include "views/OverviewView.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QListWidget>
#include <QStyle>
#include <QVBoxLayout>

namespace studio
{

OverviewView::OverviewView(QWidget *parent) : QWidget(parent)
{
    algorithm_ = new QLabel;
    algorithm_->setObjectName(QStringLiteral("pageTitle"));
    file_ = new QLabel;
    file_->setObjectName(QStringLiteral("subtle"));
    file_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    status_ = new QLabel;
    status_->setWordWrap(true);
    status_->setTextInteractionFlags(Qt::TextSelectableByMouse);

    facts_ = new QListWidget;
    facts_->setFrameShape(QFrame::NoFrame);
    facts_->setSelectionMode(QAbstractItemView::NoSelection);
    warnings_ = new QListWidget;
    warnings_->setFrameShape(QFrame::NoFrame);
    warnings_->setWordWrap(true);

    auto *factsTitle = new QLabel(tr("Specification as BayesTS reads it"));
    factsTitle->setObjectName(QStringLiteral("sectionTitle"));
    auto *warningsTitle = new QLabel(tr("Notes"));
    warningsTitle->setObjectName(QStringLiteral("sectionTitle"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(8);
    layout->addWidget(algorithm_);
    layout->addWidget(file_);
    layout->addSpacing(8);
    layout->addWidget(status_);
    layout->addSpacing(12);
    layout->addWidget(factsTitle);
    layout->addWidget(facts_, 2);
    layout->addWidget(warningsTitle);
    layout->addWidget(warnings_, 1);
    clear();
}

void OverviewView::clear()
{
    algorithm_->setText(tr("No model open"));
    file_->setText(tr("Open a BayesTS model file (.h5) with File › Open."));
    status_->clear();
    status_->setStyleSheet({});
    facts_->clear();
    warnings_->clear();
}

void OverviewView::showReport(const QString &file, const CheckReport &report)
{
    clear();
    algorithm_->setText(report.algorithm.isEmpty() ? tr("Unknown model") : report.algorithm);
    file_->setText(QDir::toNativeSeparators(QFileInfo(file).absoluteFilePath()));

    if (report.ok)
    {
        status_->setText(report.hasPosterior
                             ? tr("✓  Estimated. Posterior draws are in the file.")
                             : tr("✓  Ready to estimate. BayesTS accepts this specification."));
        status_->setStyleSheet(QStringLiteral("color: #1E7B4A; font-weight: 600;"));
    }
    else
    {
        status_->setText(tr("✗  BayesTS refuses this file: %1").arg(report.error));
        status_->setStyleSheet(QStringLiteral("color: #B3261E; font-weight: 600;"));
    }

    facts_->addItems(report.facts);
    const QIcon warn = QApplication::style()->standardIcon(QStyle::SP_MessageBoxWarning);
    for (const QString &w : report.warnings)
    {
        warnings_->addItem(new QListWidgetItem(warn, w));
    }
    if (report.warnings.isEmpty() && report.ok)
    {
        warnings_->addItem(tr("None."));
    }
}

} // namespace studio
