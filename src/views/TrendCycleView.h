// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Franz X. Mohr

#pragma once

#include <QFutureWatcher>
#include <QWidget>

#include <vector>

#include "engine/TrendCycle.h"
#include "views/FigureExport.h"

class QComboBox;
class QGridLayout;
class QLabel;
class QTabWidget;
class QTableWidget;
class QTimer;
class QwtPlot;

namespace studio
{

/// The analysis of an unobserved components model: each series with its
/// trend, and its cycle -- the output gap of a GDP series -- with 68% and 90%
/// bands over the sample, and both at a period chosen by its date in a table
/// (engine/TrendCycle.h).
class TrendCycleView : public QWidget
{
    Q_OBJECT

public:
    explicit TrendCycleView(QWidget *parent = nullptr);

    /// The model to analyse; an empty file, or one that is not an estimated
    /// unobserved components model, disables the view with the reason.
    void setModel(const QString &file);

private:
    struct Result
    {
        QString file;   ///< the model it was computed for
        QString error;
        std::vector<TrendCycleSeries> series;
    };

    void compute();
    void show(const Result &result);
    void draw();
    void fillTable();

    QString file_;
    TrendCycleSupport support_;
    Result result_;
    QFutureWatcher<Result> *watcher_;

    QLabel *status_;
    QComboBox *variable_;
    QTabWidget *tabs_;
    QWidget *plotHost_;
    QGridLayout *grid_;
    std::vector<QwtPlot *> plots_;   ///< per series shown: the trend, then the cycle
    class PeriodBox *period_;
    QTableWidget *table_;
    FigureProvider figure_;
};

} // namespace studio
