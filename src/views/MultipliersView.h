// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Franz X. Mohr

#pragma once

#include <QFutureWatcher>
#include <QWidget>

#include <vector>

#include "engine/Multipliers.h"
#include "views/FigureExport.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGridLayout;
class QLabel;
class QSpinBox;
class QTimer;
class QwtPlot;

namespace studio
{

/// The dynamic multipliers of the Application stage: the response of every
/// endogenous variable to a change in every weakly exogenous variable -- a
/// matrix laid out as the impulse responses', a column per exogenous variable
/// and a row per response -- or in one, held or for one period, with 68% and
/// 90% bands. The numbers are bvartools'
/// multipliers(); test/structural_test.cpp holds the two together draw by
/// draw.
class MultipliersView : public QWidget
{
    Q_OBJECT

public:
    explicit MultipliersView(QWidget *parent = nullptr);

    void setModel(const QString &file);

private:
    struct Result
    {
        QString error;
        std::vector<int> impulses;                   // the exogenous variables moved
        std::vector<std::vector<arma::mat>> draws;   // per impulse, per draw: horizons x responses
    };

    MultiplierRequest request() const;
    void schedule();
    void compute();
    void show(const Result &result);
    void rebuildGrid();
    /// Every exogenous variable at once, in the matrix.
    bool allImpulses() const;

    QString file_;
    MultiplierSupport support_;
    QFutureWatcher<Result> *watcher_;
    QTimer *debounce_;
    bool pending_ = false;

    QLabel *status_;
    QComboBox *impulse_;
    QComboBox *type_;
    QSpinBox *horizon_;
    QDoubleSpinBox *shock_;
    QCheckBox *cumulative_;
    QSpinBox *period_;
    QLabel *periodLabel_;
    QWidget *gridHost_;
    QGridLayout *grid_;
    std::vector<QwtPlot *> plots_;      // in the matrix: response i, exogenous j at i * m + j
    std::vector<QWidget *> headers_;
    int columns_ = 1;
    bool matrix_ = false;
    FigureProvider figure_;
};

} // namespace studio
