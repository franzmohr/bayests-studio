// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Franz X. Mohr

#include "views/FoldablePanes.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QPainter>
#include <QSplitter>
#include <QStackedLayout>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <numeric>

namespace studio
{

FoldPane::FoldPane(const QString &id, const QString &title, QWidget *content, QWidget *parent)
    : QWidget(parent), id_(id), title_(title), content_(content)
{
    toggle_ = new QToolButton;
    toggle_->setText(title);
    toggle_->setArrowType(Qt::DownArrow);
    toggle_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toggle_->setAutoRaise(true);
    QFont bold = toggle_->font();
    bold.setBold(true);
    toggle_->setFont(bold);
    toggle_->setToolTip(tr("Folds the pane away, giving its room to the others; click again to unfold it."));
    connect(toggle_, &QToolButton::clicked, this, [this]() {
        setFolded(!folded_);
        emit foldToggled();
    });

    auto *focus = new QToolButton;
    focus->setIcon(QApplication::style()->standardIcon(QStyle::SP_TitleBarMaxButton));
    focus->setAutoRaise(true);
    focus->setToolTip(tr("Shows this pane alone, the others folded: the workbench one pane at a time, for a "
                         "smaller screen. Click again to unfold them all."));
    connect(focus, &QToolButton::clicked, this, &FoldPane::focusRequested);

    header_ = new QWidget;
    auto *h = new QHBoxLayout(header_);
    h->setContentsMargins(0, 0, 0, 0);
    h->addWidget(toggle_);
    h->addStretch(1);
    h->addWidget(focus);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 4, 6, 0);
    layout->setSpacing(2);
    layout->addWidget(header_);
    layout->addWidget(content_, 1);
}

void FoldPane::setFolded(bool folded)
{
    folded_ = folded;
    toggle_->setArrowType(folded ? Qt::RightArrow : Qt::DownArrow);
    content_->setVisible(!folded);
    setMaximumHeight(folded ? headerHeight() : QWIDGETSIZE_MAX);
}

int FoldPane::headerHeight() const
{
    const QMargins m = layout()->contentsMargins();
    return header_->sizeHint().height() + m.top() + m.bottom() + 2;
}

FoldStrip::FoldStrip(QWidget *parent) : QAbstractButton(parent)
{
    setCursor(Qt::PointingHandCursor);
    setToolTip(tr("Unfolds the panes of this column."));
}

QSize FoldStrip::sizeHint() const
{
    const QFontMetrics fm(font());
    return {fm.height() + 14, fm.horizontalAdvance(text()) + 48};
}

QSize FoldStrip::minimumSizeHint() const
{
    const QFontMetrics fm(font());
    return {fm.height() + 14, 60};
}

void FoldStrip::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QPalette &pal = palette();
    p.fillRect(rect(), underMouse() ? pal.color(QPalette::Midlight) : pal.color(QPalette::Button));
    p.setPen(pal.color(QPalette::Mid));
    p.drawLine(rect().topRight(), rect().bottomRight());
    // The titles read top to bottom, after an arrow that says it unfolds.
    p.translate(width(), 0);
    p.rotate(90);
    QFont f = font();
    f.setBold(true);
    p.setFont(f);
    p.setPen(pal.color(QPalette::ButtonText));
    p.drawText(QRect(12, 0, height() - 24, width()), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("▸  ") + text());
}

FoldColumn::FoldColumn(const std::vector<FoldPane *> &panes, int weight, int minimumWidth,
                       const std::vector<int> &paneWeights, QWidget *parent)
    : QWidget(parent), panes_(panes), paneWeights_(paneWeights), weight_(weight), minimumWidth_(minimumWidth)
{
    splitter_ = new QSplitter(Qt::Vertical);
    splitter_->setChildrenCollapsible(false);
    QStringList titles;
    for (FoldPane *pane : panes_)
    {
        splitter_->addWidget(pane);
        titles << pane->title();
        connect(pane, &FoldPane::foldToggled, this, [this]() {
            relayout();
            emit changed();
        });
    }
    paneWeights_.resize(panes_.size(), 1);

    strip_ = new FoldStrip;
    strip_->setText(titles.join(QStringLiteral("  ·  ")));
    connect(strip_, &FoldStrip::clicked, this, [this]() {
        for (FoldPane *pane : panes_)
        {
            pane->setFolded(false);
        }
        relayout();
        emit changed();
    });

    auto *holder = new QWidget;
    auto *v = new QVBoxLayout(holder);
    v->setContentsMargins(0, 0, 0, 0);
    v->addWidget(splitter_);
    stack_ = new QStackedLayout(this);
    stack_->setContentsMargins(0, 0, 0, 0);
    stack_->addWidget(holder);
    stack_->addWidget(strip_);
    relayout();
}

bool FoldColumn::allFolded() const
{
    return std::all_of(panes_.begin(), panes_.end(), [](const FoldPane *p) { return p->isFolded(); });
}

int FoldColumn::stripWidth() const
{
    return strip_->sizeHint().width();
}

void FoldColumn::relayout()
{
    if (allFolded())
    {
        stack_->setCurrentWidget(strip_);
        setMinimumWidth(stripWidth());
        setMaximumWidth(stripWidth());
        return;
    }
    stack_->setCurrentIndex(0);
    setMinimumWidth(minimumWidth_);
    setMaximumWidth(QWIDGETSIZE_MAX);
    // A folded pane keeps its header; the unfolded share the rest by weight.
    const int total = std::max(splitter_->height(), 400);
    int headers = 0, weights = 0;
    for (std::size_t i = 0; i < panes_.size(); ++i)
    {
        if (panes_[i]->isFolded())
        {
            headers += panes_[i]->headerHeight();
        }
        else
        {
            weights += paneWeights_[i];
        }
    }
    QList<int> sizes;
    for (std::size_t i = 0; i < panes_.size(); ++i)
    {
        sizes << (panes_[i]->isFolded() ? panes_[i]->headerHeight()
                                        : std::max(1, (total - headers) * paneWeights_[i] / std::max(1, weights)));
    }
    splitter_->setSizes(sizes);
}

} // namespace studio
