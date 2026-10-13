// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Franz X. Mohr

#pragma once

#include <QAbstractButton>
#include <QWidget>

#include <vector>

class QSplitter;
class QStackedLayout;
class QToolButton;

namespace studio
{

/// One pane of the workbench: a header that folds it away -- its title on a
/// button -- and a button that shows it alone, the others folded, which is
/// how the workbench is used one pane at a time on a laptop.
class FoldPane : public QWidget
{
    Q_OBJECT

public:
    FoldPane(const QString &id, const QString &title, QWidget *content, QWidget *parent = nullptr);

    QString id() const { return id_; }
    QString title() const { return title_; }
    bool isFolded() const { return folded_; }
    void setFolded(bool folded);
    int headerHeight() const;

signals:
    void foldToggled();
    void focusRequested();

private:
    QString id_;
    QString title_;
    bool folded_ = false;
    QWidget *header_;
    QToolButton *toggle_;
    QWidget *content_;
};

/// A folded column: its panes' titles turned on their side in a narrow
/// strip, which unfolds them when clicked.
class FoldStrip : public QAbstractButton
{
    Q_OBJECT

public:
    explicit FoldStrip(QWidget *parent = nullptr);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
};

/// A column of the workbench: its panes stacked in a splitter, a fold of
/// one giving its height to the others, and the narrow strip in their place
/// once every pane is folded.
class FoldColumn : public QWidget
{
    Q_OBJECT

public:
    /// `weight` is the column's share of the width; `paneWeights` the panes'
    /// shares of the height; `minimumWidth` what an unfolded column needs.
    FoldColumn(const std::vector<FoldPane *> &panes, int weight, int minimumWidth,
               const std::vector<int> &paneWeights, QWidget *parent = nullptr);

    const std::vector<FoldPane *> &panes() const { return panes_; }
    bool allFolded() const;
    int weight() const { return weight_; }
    int unfoldedMinimum() const { return minimumWidth_; }
    int stripWidth() const;

    /// The strip or the panes, and the panes' heights, after a fold.
    void relayout();

signals:
    /// A pane of the column was folded or unfolded through its controls.
    void changed();

private:
    std::vector<FoldPane *> panes_;
    std::vector<int> paneWeights_;
    int weight_;
    int minimumWidth_;
    QSplitter *splitter_;
    FoldStrip *strip_;
    QStackedLayout *stack_;
};

} // namespace studio
