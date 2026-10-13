// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Franz X. Mohr

#pragma once

#include <QWidget>

#include <vector>

#include "engine/DataSet.h"
#include "engine/ModelRunner.h"

class QComboBox;
class QLabel;
class QPushButton;
class QTabWidget;
class QTableWidget;
class QTreeView;

namespace studio
{

class DiagnosticsView;
class LiveBandsView;
class SummaryPlotView;
class InclusionView;
class HorseRaceView;
class ModelFileTree;
class OverviewView;
class SeriesPlot;
class HistoricalDecompositionView;
class MultipliersView;
class SpilloverView;
class StructuralView;

/// The stages of the workflow in Mohr's bvartools article (Section 3,
/// Figure 1): data, model set-up, posterior simulation, evaluation and
/// application. The order of this enum is the order of the stage bar.
enum class StageId
{
    Data,
    Setup,
    Posterior,
    Evaluation,
    Application,
};

/// The estimation sample of a model file, /data/train/y, one vector per
/// variable, and the variable names the builder writes beside it.
std::vector<std::vector<double>> sampleSeries(const QString &file, int k);
QStringList sampleNames(const QString &file);

/// What every page is told when the current model changes or a run ends.
struct ModelContext
{
    QString file;              ///< empty when no model is selected
    const ModelFileTree *tree = nullptr;
    CheckReport report;
    QStringList projectFiles;  ///< every model of the project, for comparisons
    QString projectDir;        ///< the project folder; empty for a single file
};

class StagePage : public QWidget
{
    Q_OBJECT

public:
    using QWidget::QWidget;
    virtual void showModel(const ModelContext &context) = 0;

signals:
    void runRequested(const std::vector<studio::Stage> &stages);
    void importRequested();
    void newModelRequested();
    /// A model file derived from the current one, to join the project.
    void modelCreated(const QString &file);
};

/// 1. Data: the series the model is estimated on.
class DataPage : public StagePage
{
    Q_OBJECT

public:
    explicit DataPage(QWidget *parent = nullptr);
    void showModel(const ModelContext &context) override;

    /// An imported data set takes the page over from the model's sample
    /// until another one is imported.
    void showDataSet(const DataSet &data);

private:
    void redraw();

    ModelContext context_;
    DataSet data_;
    QLabel *summary_;
    QPushButton *newModel_;
    SeriesPlot *plot_;
};

/// 2. Model set-up: data matrices and specification, prior hyperparameters,
/// initial values -- each as BayesTS reads it from the model file.
class SetupPage : public StagePage
{
    Q_OBJECT

public:
    SetupPage(ModelFileTree *tree, QWidget *parent = nullptr);
    void showModel(const ModelContext &context) override;
    void setDataAvailable(bool available);

private:
    QPushButton *newModel_;
    ModelFileTree *tree_;
    QTabWidget *tabs_;
    OverviewView *overview_;
    QTreeView *priors_;
    QTreeView *initial_;
    QTreeView *file_;
};

/// 3. Posterior simulation: the chain's settings, the run, and the draws --
/// live for a time-varying coefficient path, and per parameter afterwards.
class PosteriorPage : public StagePage
{
    Q_OBJECT

public:
    PosteriorPage(QtReporter *reporter, QWidget *parent = nullptr);
    void showModel(const ModelContext &context) override;

    void runStarted();
    void runFinished();
    /// Without the page title, for a panel of the workbench.
    void setCompact();

private:
    void showDataset(int index);

    ModelContext context_;
    QStringList names_;
    QLabel *algorithm_;
    QLabel *chain_;
    QPushButton *simulate_;
    QTabWidget *tabs_;
    LiveBandsView *live_;
    QComboBox *dataset_;
    DiagnosticsView *draws_;
    SummaryPlotView *summary_;
    InclusionView *inclusion_;
    class VolatilityBreakView *volatility_;
    int volatilityTab_ = -1;
    class StateVarianceView *stateVariance_;
    int stateVarianceTab_ = -1;
};

/// 4. Evaluation: information criteria and forecast performance, across all
/// models of the project so that they can be compared.
class EvaluationPage : public StagePage
{
    Q_OBJECT

public:
    explicit EvaluationPage(QWidget *parent = nullptr);
    void showModel(const ModelContext &context) override;

private:
    QPushButton *loglik_;
    QTableWidget *table_;
    QLabel *explanation_;
    QTabWidget *tabs_;
    HorseRaceView *race_;
    QString raceFolder_;
};

/// 5. Application: forecasting, structural analysis and spillovers.
class ApplicationPage : public StagePage
{
    Q_OBJECT

public:
    explicit ApplicationPage(QWidget *parent = nullptr);
    void showModel(const ModelContext &context) override;
    void showStructural();

    /// One of the page's tabs alone, without the title or the tab bar: a
    /// panel of the workbench.
    enum class Panel { Forecasting, Structural };
    void showOnly(Panel panel);

private:
    void showVariable(int variable);

    ModelContext context_;
    QTabWidget *tabs_;
    QPushButton *forecast_;
    QComboBox *variable_;
    QLabel *forecastNote_;
    SeriesPlot *fan_;
    StructuralView *structural_;
    SpilloverView *spillover_;
    MultipliersView *multipliers_;
    HistoricalDecompositionView *historical_;
};

} // namespace studio
