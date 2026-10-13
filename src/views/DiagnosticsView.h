// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Franz X. Mohr

#pragma once

#include <QHash>
#include <QWidget>

#include <vector>

#include "engine/ModelFileTree.h"

class QComboBox;
class QGridLayout;
class QLabel;
class QStackedWidget;
class QwtPlot;
class QwtPlotCurve;
class QwtPlotHistogram;

namespace studio
{

/// How the rows of a draws dataset form a matrix: the coefficients of a VAR
/// an equation per row and a regressor per column, a covariance or precision
/// k x k, a VEC's beta a term per row and a relation per column. Cells
/// without a parameter hold -1.
struct BlockLayout
{
    QStringList rowLabels;
    QStringList columnLabels;
    std::vector<int> cell;   ///< row-major, rows x columns: the dataset row, or -1

    bool valid() const { return !rowLabels.isEmpty() && !columnLabels.isEmpty(); }
    int rows() const { return static_cast<int>(rowLabels.size()); }
    int columns() const { return static_cast<int>(columnLabels.size()); }
};

/// The layout of a block from the names of its rows -- "equation ← regressor"
/// as the model builder writes them, "ec1: term" for beta -- or, unnamed, a
/// k x k block of the k variables, or k of them on a diagonal (a diagonal
/// precision). Invalid when none applies.
BlockLayout blockLayout(std::size_t rows, const QStringList &rowNames, const QStringList &variables);

/// Trace and marginal density of one row of a draws dataset, with summary
/// statistics -- the first look at whether a chain mixed -- or the whole
/// block at once, laid out as its matrix.
class DiagnosticsView : public QWidget
{
    Q_OBJECT

public:
    explicit DiagnosticsView(QWidget *parent = nullptr);

    /// Shows the dataset at `path` of `file`; rows are quantities, columns
    /// draws, as BayesTS writes them. `variables`, the model's, lay out an
    /// unnamed k x k block. With `periods`, a block whose rows are that many
    /// times a layout's -- a time-varying path, stacked period by period, of
    /// coefficients, of a covariance or of a diagonal -- is shown at a period
    /// chosen.
    void showDataset(const QString &file, const QString &path, const QStringList &rowNames = {},
                     const QStringList &variables = {}, int periods = 0);
    void clear();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void showRow(int row);
    void modeChanged();
    void buildBlock();
    void clearBlock();

    Matrix matrix_;
    QString path_;
    QStringList names_;
    BlockLayout layout_;
    QLabel *title_;
    QComboBox *mode_;
    QComboBox *row_;
    QLabel *rowLabel_;
    QStackedWidget *pages_;
    QLabel *stats_;
    QwtPlot *trace_;
    QwtPlot *density_;
    QwtPlotCurve *traceCurve_;
    QwtPlotCurve *meanCurve_;
    QwtPlotHistogram *histogram_;

    // The whole block.
    QComboBox *show_;
    QComboBox *period_;              ///< a time-varying block's period
    QLabel *periodLabel_;
    int pathCoefficients_ = 0;       ///< coefficients per period of a path, 0 for none
    QLabel *blockNote_;
    QWidget *grid_;
    QGridLayout *gridLayout_;
    std::vector<QwtPlot *> cells_;          ///< row-major as layout_.cell, nullptr where empty
    QHash<QObject *, int> cellRows_;        ///< a cell's canvas: its dataset row
    bool blockBuilt_ = false;
};

} // namespace studio
