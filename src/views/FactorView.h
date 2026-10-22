// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Franz X. Mohr

#pragma once

#include <QFutureWatcher>
#include <QWidget>

#include <vector>

#include "engine/FactorAnalysis.h"
#include "views/FigureExport.h"

class QComboBox;
class QGridLayout;
class QLabel;
class QListWidget;
class QSpinBox;
class QTabWidget;
class QTableWidget;
class QTimer;
class QwtPlot;

namespace studio
{

/// The analysis of a dynamic factor model or a FAVAR: the factors' paths, the
/// loadings, the share of each series' variance the factors explain, and the
/// impulse responses of the panel -- and a FAVAR's observed factors -- to the
/// state equation's shocks, from the posterior draws in the file
/// (engine/FactorAnalysis.h). A model whose loadings, transition or factor
/// variances move is read at a period chosen by its date.
///
/// Recomputed a moment after a control changes, on a worker thread.
class FactorView : public QWidget
{
    Q_OBJECT

public:
    explicit FactorView(QWidget *parent = nullptr);

    /// The model to analyse; an empty file, or one that is not an estimated
    /// factor model, disables the view with the reason.
    void setModel(const QString &file);

private:
    struct Result
    {
        QString file;   ///< the model it was computed for
        QString error;
        std::vector<PathBands> paths;
        LoadingTable loadings;
        ExplainedShare share;
        QString responsesError;
        std::vector<arma::mat> responses;   ///< per draw: horizons x (series, observed factors)
    };

    void schedule();
    void compute();
    void show(const Result &result);
    void drawFactors();
    void drawResponses();
    void fillTables();
    QStringList responseNames() const;

    QString file_;
    FactorSupport support_;
    Result result_;
    QFutureWatcher<Result> *watcher_;
    QTimer *debounce_;
    bool pending_ = false;

    QLabel *status_;
    QTabWidget *tabs_;
    class PeriodBox *period_;
    QLabel *periodLabel_;
    QComboBox *shock_;
    QSpinBox *horizon_;

    QWidget *factorHost_;
    QGridLayout *factorGrid_;
    std::vector<QwtPlot *> factorPlots_;
    QTableWidget *loadings_;
    QLabel *loadingsNote_;
    QTableWidget *share_;
    QListWidget *responseList_;
    QLabel *responsesNote_;
    QWidget *responseHost_;
    QGridLayout *responseGrid_;
    std::vector<QwtPlot *> responsePlots_;
    std::vector<int> responseColumns_;   ///< the column of the result each plot shows
    FigureProvider factorFigure_, responseFigure_;
};

} // namespace studio
