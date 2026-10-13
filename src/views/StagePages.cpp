// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Franz X. Mohr

#include "views/StagePages.h"

#include "engine/Criteria.h"
#include "engine/Structural.h"
#include "engine/ModelFileTree.h"
#include "views/DiagnosticsView.h"
#include "views/LiveBandsView.h"
#include "views/OverviewView.h"
#include "views/SeriesPlot.h"
#include "views/HistoricalDecompositionView.h"
#include "views/MultipliersView.h"
#include "views/SpilloverView.h"
#include "views/SummaryPlotView.h"
#include "views/TableExport.h"
#include "views/InclusionView.h"
#include "views/StateVarianceView.h"
#include "views/VolatilityBreakView.h"
#include "engine/VolatilityBreak.h"
#include "views/HorseRaceView.h"
#include "views/StructuralView.h"

#include <QComboBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTabBar>
#include <QTabWidget>
#include <QTreeView>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

namespace studio
{

namespace
{

QLabel *pageTitle(const QString &text)
{
    auto *label = new QLabel(text);
    label->setObjectName(QStringLiteral("pageTitle"));
    return label;
}

QLabel *subtle(const QString &text = {})
{
    auto *label = new QLabel(text);
    label->setObjectName(QStringLiteral("subtle"));
    label->setWordWrap(true);
    return label;
}

QVBoxLayout *pageLayout(QWidget *page)
{
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 18, 24, 18);
    layout->setSpacing(10);
    return layout;
}

QTreeView *treeView(ModelFileTree *tree)
{
    auto *view = new QTreeView;
    view->setModel(tree);
    view->setUniformRowHeights(true);
    view->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    view->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    view->header()->setStretchLastSection(false);
    return view;
}

/// Points a view at one group of the file, or hides its contents when the
/// group is absent.
void rootAt(QTreeView *view, const ModelFileTree *tree, const QString &path)
{
    const H5Node *target = tree->find(path);
    QModelIndex index;
    if (target != nullptr && target->parent != nullptr)
    {
        // Walk down from the root to build the index.
        std::function<QModelIndex(const QModelIndex &)> search = [&](const QModelIndex &parent) {
            for (int r = 0; r < tree->rowCount(parent); ++r)
            {
                const QModelIndex child = tree->index(r, 0, parent);
                if (tree->node(child) == target)
                {
                    return child;
                }
                if (path.startsWith(tree->node(child)->path + "/"))
                {
                    return search(child);
                }
            }
            return QModelIndex();
        };
        index = search({});
    }
    view->setRootIndex(index);
    view->setEnabled(target != nullptr);
    view->expandAll();
}

/// /data/train/y is one row stacked by period: element t*k + i.
std::vector<std::vector<double>> seriesOf(const QString &file, int k)
{
    // Either spelling BayesTS reads: one row stacked by period, element t*k + i,
    // or the time series bvartools and the model builder write, one row per
    // variable and one column per period.
    const Matrix y = readMatrix(file, QStringLiteral("/data/train/y"));
    const std::size_t n = y.rows * y.cols;
    const auto kk = static_cast<std::size_t>(k);
    std::vector<std::vector<double>> series(kk, std::vector<double>(n / kk));
    const bool byVariable = y.rows == kk && kk > 1;
    for (std::size_t j = 0; j < n; ++j)
    {
        if (byVariable)
        {
            series[j / y.cols][j % y.cols] = y.values[j];
        }
        else
        {
            series[j % kk][j / kk] = y.values[j];
        }
    }
    return series;
}

/// The names the model builder writes on /data/train/y; empty for a file
/// written elsewhere.
QStringList variableNames(const QString &file)
{
    return readStringsAttribute(file, QStringLiteral("/data/train/y"), QStringLiteral("variables"));
}

} // namespace

std::vector<std::vector<double>> sampleSeries(const QString &file, int k)
{
    return seriesOf(file, k);
}

QStringList sampleNames(const QString &file)
{
    return variableNames(file);
}

// --------------------------------------------------------------------------
// 1. Data

DataPage::DataPage(QWidget *parent) : StagePage(parent)
{
    auto *layout = pageLayout(this);
    layout->addWidget(pageTitle(tr("Data")));
    summary_ = subtle();
    layout->addWidget(summary_);

    auto *import = new QPushButton(tr("Import Data…"));
    import->setToolTip(tr("A CSV or other delimited file: a header row of names, one row per "
                          "period, optionally a first column of period labels."));
    connect(import, &QPushButton::clicked, this, &StagePage::importRequested);
    newModel_ = new QPushButton(tr("New Model from These Data…"));
    newModel_->setEnabled(false);
    connect(newModel_, &QPushButton::clicked, this, &StagePage::newModelRequested);
    auto *bar = new QHBoxLayout;
    newModel_->setToolTip(tr("The model builder on the imported data: choose the model, the priors and the sampler, and BayesTS checks the file before it is written."));
    bar->addWidget(import);
    bar->addWidget(newModel_);
    bar->addStretch(1);
    layout->addLayout(bar);

    plot_ = new SeriesPlot(tr("Period"), tr("Value"));
    layout->addWidget(plot_, 1);
}

void DataPage::showModel(const ModelContext &context)
{
    context_ = context;
    redraw();
}

void DataPage::showDataSet(const DataSet &data)
{
    data_ = data;
    newModel_->setEnabled(!data_.empty());
    redraw();
}

void DataPage::redraw()
{
    plot_->clear();
    if (!data_.empty())
    {
        for (int c = 0; c < data_.series(); ++c)
        {
            const arma::vec col = data_.values.col(static_cast<arma::uword>(c));
            plot_->addLine(data_.names[c], std::vector<double>(col.begin(), col.end()), c);
        }
        summary_->setText(tr("Imported from %1: %2 series over %3 periods, %4 to %5.")
                              .arg(QFileInfo(data_.source).fileName())
                              .arg(data_.series())
                              .arg(data_.length())
                              .arg(data_.periodLabel(0), data_.periodLabel(data_.length() - 1)));
        plot_->replot();
        return;
    }
    if (context_.file.isEmpty() || context_.report.k <= 0)
    {
        summary_->setText(tr("Import a data set to build a model from it, or select a model of the project."));
        return;
    }
    try
    {
        const auto series = seriesOf(context_.file, context_.report.k);
        const QStringList names = variableNames(context_.file);
        for (std::size_t i = 0; i < series.size(); ++i)
        {
            const auto n = static_cast<qsizetype>(i);
            plot_->addLine(n < names.size() ? names[n] : tr("Variable %1").arg(i + 1), series[i],
                           static_cast<int>(i));
        }
        summary_->setText(tr("Estimation sample of the selected model (/data/train/y): %1 variables over %2 periods.")
                              .arg(context_.report.k)
                              .arg(series.empty() ? 0 : series.front().size()));
    }
    catch (const std::exception &e)
    {
        summary_->setText(tr("The data cannot be shown: %1").arg(QString::fromUtf8(e.what())));
    }
    plot_->replot();
}

// --------------------------------------------------------------------------
// 2. Model set-up

SetupPage::SetupPage(ModelFileTree *tree, QWidget *parent) : StagePage(parent), tree_(tree)
{
    auto *layout = pageLayout(this);
    layout->addWidget(pageTitle(tr("Model set-up")));
    layout->addWidget(subtle(tr("Data matrices, prior hyperparameters and initial values, as BayesTS "
                                "reads them from the selected model file.")));
    newModel_ = new QPushButton(tr("New Model…"));
    newModel_->setEnabled(false);
    newModel_->setToolTip(tr("Import a data set in the Data stage first."));
    connect(newModel_, &QPushButton::clicked, this, &StagePage::newModelRequested);
    auto *bar = new QHBoxLayout;
    bar->addWidget(newModel_);
    bar->addStretch(1);
    layout->addLayout(bar);

    overview_ = new OverviewView;
    priors_ = treeView(tree);
    initial_ = treeView(tree);
    file_ = treeView(tree);

    tabs_ = new QTabWidget;
    tabs_->setDocumentMode(true);
    tabs_->addTab(overview_, tr("Specification"));
    tabs_->addTab(priors_, tr("Prior hyperparameters"));
    tabs_->addTab(initial_, tr("Initial values"));
    tabs_->addTab(file_, tr("Model file"));
    layout->addWidget(tabs_, 1);
}

void SetupPage::setDataAvailable(bool available)
{
    newModel_->setEnabled(available);
    newModel_->setToolTip(available ? tr("Specify a model of the imported data with tick boxes.")
                                    : tr("Import a data set in the Data stage first."));
}

void SetupPage::showModel(const ModelContext &context)
{
    if (context.file.isEmpty())
    {
        overview_->clear();
    }
    else
    {
        overview_->showReport(context.file, context.report);
    }
    rootAt(priors_, tree_, QStringLiteral("/priors"));
    rootAt(initial_, tree_, QStringLiteral("/initial"));
    file_->setRootIndex({});
    file_->expandToDepth(0);
}

// --------------------------------------------------------------------------
// 3. Posterior simulation

PosteriorPage::PosteriorPage(QtReporter *reporter, QWidget *parent) : StagePage(parent)
{
    auto *layout = pageLayout(this);
    layout->addWidget(pageTitle(tr("Posterior simulation")));

    algorithm_ = new QLabel;
    algorithm_->setObjectName(QStringLiteral("sectionTitle"));
    chain_ = subtle();
    simulate_ = new QPushButton(tr("Run Posterior Simulation"));
    simulate_->setDefault(true);
    simulate_->setToolTip(tr("Draws from the posterior of the selected model and writes the draws into its file. A model already estimated is not estimated again."));
    connect(simulate_, &QPushButton::clicked, this, [this]() { emit runRequested({Stage::Coefficients}); });

    auto *bar = new QHBoxLayout;
    auto *text = new QVBoxLayout;
    text->addWidget(algorithm_);
    text->addWidget(chain_);
    bar->addLayout(text, 1);
    bar->addWidget(simulate_, 0, Qt::AlignTop);
    layout->addLayout(bar);

    live_ = new LiveBandsView(reporter);

    auto *drawsPage = new QWidget;
    auto *drawsLayout = new QVBoxLayout(drawsPage);
    drawsLayout->setContentsMargins(0, 8, 0, 0);
    dataset_ = new QComboBox;
    dataset_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    dataset_->setToolTip(tr("The block of parameters whose draws the trace and the histogram show."));
    connect(dataset_, &QComboBox::currentIndexChanged, this, &PosteriorPage::showDataset);
    auto *pick = new QHBoxLayout;
    pick->addWidget(new QLabel(tr("Parameter block")));
    pick->addWidget(dataset_);
    pick->addStretch(1);
    drawsLayout->addLayout(pick);
    draws_ = new DiagnosticsView;
    drawsLayout->addWidget(draws_, 1);

    tabs_ = new QTabWidget;
    tabs_->setDocumentMode(true);
    tabs_->addTab(live_, tr("Time-varying coefficients"));
    tabs_->addTab(drawsPage, tr("Draws and convergence"));
    summary_ = new SummaryPlotView;
    tabs_->addTab(summary_, tr("Summary plot"));
    inclusion_ = new InclusionView;
    tabs_->addTab(inclusion_, tr("Inclusion probabilities"));
    volatility_ = new VolatilityBreakView;
    volatilityTab_ = tabs_->addTab(volatility_, tr("Volatility break"));
    stateVariance_ = new StateVarianceView;
    stateVarianceTab_ = tabs_->addTab(stateVariance_, tr("State variance"));
    tabs_->setObjectName(QStringLiteral("posteriorTabs"));
    layout->addWidget(tabs_, 1);
}

void PosteriorPage::showModel(const ModelContext &context)
{
    context_ = context;
    const CheckReport &r = context.report;
    const bool have = !context.file.isEmpty();
    simulate_->setEnabled(have && r.ok);

    if (!have)
    {
        algorithm_->setText(tr("No model selected"));
        chain_->clear();
    }
    else
    {
        algorithm_->setText(r.algorithm);
        chain_->setText(tr("%1 kept draws after a burn-in of %2, thinned by %3. Seed: %4. %5")
                            .arg(r.iterations)
                            .arg(r.burnin)
                            .arg(r.thin)
                            .arg(r.seed.isEmpty() ? tr("none (not reproducible)") : r.seed)
                            .arg(r.hasPosterior ? tr("Draws are in the file.") : tr("Not estimated yet.")));
    }

    int coefficients = 0;
    if (const H5Node *coeffs = context.tree ? context.tree->find(QStringLiteral("/posterior/a/coeffs")) : nullptr;
        coeffs != nullptr && !coeffs->dims.empty() && r.periods > 0)
    {
        coefficients = static_cast<int>(coeffs->dims[0]) / r.periods;
    }
    const QStringList names =
        have ? readStringsAttribute(context.file, QStringLiteral("/data/train/z"), QStringLiteral("coefficients"))
             : QStringList();
    names_ = names;
    live_->setModel(have && r.timeVarying, r.periods, coefficients, names);
    if (have)
    {
        live_->showStored(context.file);
    }
    tabs_->setTabEnabled(0, have && r.timeVarying);
    summary_->setModel(have && r.hasPosterior ? context.file : QString());
    inclusion_->setModel(have && r.hasPosterior ? context.file : QString());
    const bool vbreak = have && r.hasPosterior && hasVolatilityBreak(context.file);
    volatility_->setModel(vbreak ? context.file : QString());
    tabs_->setTabEnabled(volatilityTab_, vbreak);
    const bool tvp = have && r.timeVarying;
    if (stateVarianceTab_ >= 0)
    {
        stateVariance_->setModel(tvp ? context.file : QString(), names);
        tabs_->setTabEnabled(stateVarianceTab_, tvp);
    }
    if (!tvp && stateVarianceTab_ >= 0 && tabs_->currentIndex() == stateVarianceTab_)
    {
        tabs_->setCurrentIndex(1);
    }
    if (!vbreak && tabs_->currentIndex() == volatilityTab_)
    {
        tabs_->setCurrentIndex(1);
    }
    if (!(have && r.timeVarying))
    {
        tabs_->setCurrentIndex(1);
    }

    // Every two-dimensional draws dataset under /posterior, forecasts and the
    // log likelihood aside: those have stages of their own.
    const QSignalBlocker block(dataset_);
    dataset_->clear();
    if (context.tree != nullptr)
    {
        std::function<void(const H5Node *)> walk = [&](const H5Node *node) {
            for (const auto &child : node->children)
            {
                if (child->kind == H5Node::Kind::Group)
                {
                    if (child->name != QLatin1String("forecast"))
                    {
                        walk(child.get());
                    }
                }
                else if (child->dims.size() == 2 && child->dims[1] > 1 &&
                         child->path != QLatin1String("/posterior/loglik"))
                {
                    dataset_->addItem(child->path.mid(QStringLiteral("/posterior/").size()), child->path);
                }
            }
        };
        if (const H5Node *posterior = context.tree->find(QStringLiteral("/posterior")))
        {
            walk(posterior);
        }
    }
    if (dataset_->count() > 0)
    {
        showDataset(0);
    }
    else
    {
        draws_->clear();
    }
}

void PosteriorPage::showDataset(int index)
{
    if (index >= 0)
    {
        const QString path = dataset_->itemData(index).toString();
        // A constant model's a/coeffs has one row per coefficient, which the
        // names describe; a time-varying one has one per coefficient and period.
        QStringList rows;
        if (path == QLatin1String("/posterior/a/coeffs") && !context_.report.timeVarying)
        {
            rows = names_;
        }
        else if (path == QLatin1String("/posterior/beta/coeffs") && !context_.report.timeVarying)
        {
            // vec(beta), k_beta x rank, column by column: one relation after the other.
            const QStringList terms =
                readStringsAttribute(context_.file, QStringLiteral("/data/train/w"), QStringLiteral("terms"));
            for (int r = 0; !terms.isEmpty() && r < 16; ++r)
            {
                for (const QString &term : terms)
                {
                    rows << QStringLiteral("ec%1: %2").arg(r + 1).arg(term);
                }
            }
        }
        draws_->showDataset(context_.file, path, rows);
    }
}

void PosteriorPage::setCompact()
{
    // The workbench shows the state variance beside the priors instead.
    tabs_->removeTab(stateVarianceTab_);
    stateVariance_->deleteLater();
    stateVariance_ = nullptr;
    stateVarianceTab_ = -1;
    if (auto *title = findChild<QLabel *>(QStringLiteral("pageTitle")))
    {
        title->hide();
    }
    layout()->setContentsMargins(8, 6, 8, 6);
}

void PosteriorPage::runStarted()
{
    simulate_->setEnabled(false);
    if (context_.report.timeVarying)
    {
        tabs_->setCurrentIndex(0);
    }
    live_->runStarted();
}

void PosteriorPage::runFinished()
{
    live_->runFinished();
}

// --------------------------------------------------------------------------
// 4. Evaluation

EvaluationPage::EvaluationPage(QWidget *parent) : StagePage(parent)
{
    auto *outer = pageLayout(this);
    outer->addWidget(pageTitle(tr("Evaluation")));
    auto *comparison = new QWidget;
    auto *layout = new QVBoxLayout(comparison);
    layout->setContentsMargins(0, 8, 0, 0);
    race_ = new HorseRaceView;
    tabs_ = new QTabWidget;
    tabs_->setDocumentMode(true);
    tabs_->setObjectName(QStringLiteral("evaluationTabs"));
    tabs_->addTab(comparison, tr("Model comparison"));
    tabs_->addTab(race_, tr("Horse race"));
    outer->addWidget(tabs_, 1);
    explanation_ = subtle(tr(
        "Information criteria from the pointwise log likelihood, for every model of the project. "
        "WAIC = −2 (lppd − p_WAIC); smaller is better. The log predictive score sums the log "
        "predictive density of the test sample (/data/test/y); larger is better."));
    layout->addWidget(explanation_);

    loglik_ = new QPushButton(tr("Compute Log Likelihood"));
    loglik_->setToolTip(tr("Writes the pointwise log likelihood of the selected model, which lppd and WAIC are computed from."));
    connect(loglik_, &QPushButton::clicked, this, [this]() { emit runRequested({Stage::LogLikelihood}); });
    auto *bar = new QHBoxLayout;
    bar->addWidget(loglik_);
    bar->addStretch(1);
    layout->addLayout(bar);

    table_ = new QTableWidget(0, 7);
    table_->setHorizontalHeaderLabels({tr("Model"), tr("Algorithm"), tr("Periods"), tr("lppd"),
                                       tr("p_WAIC"), tr("WAIC"), tr("Log predictive score")});
    table_->horizontalHeaderItem(2)->setToolTip(tr("Periods the model explains: the sample after the initial lags."));
    table_->horizontalHeaderItem(3)->setToolTip(tr("The log pointwise predictive density: how well the posterior fits each period, summed over the sample (Gelman, Hwang and Vehtari 2014). Larger is better."));
    table_->horizontalHeaderItem(4)->setToolTip(tr("The effective number of parameters: the variance of each period's log likelihood over the draws, summed. It is what WAIC charges for flexibility."));
    table_->horizontalHeaderItem(5)->setToolTip(tr("The widely applicable information criterion of Watanabe (2010), -2 (lppd - p_WAIC), on the deviance scale: smaller is better. Compare models estimated on the same periods."));
    table_->horizontalHeaderItem(6)->setToolTip(tr("The log predictive density of the held-out periods, /data/test/y: an out-of-sample score. Larger is better."));
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table_->verticalHeader()->setVisible(false);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSortingEnabled(true);
    table_->setAlternatingRowColors(true);
    attachTableExport(table_);
    bar->addWidget(tableExportButton(table_));
    layout->addWidget(table_, 1);
}

void EvaluationPage::showModel(const ModelContext &context)
{
    if (context.projectDir != raceFolder_)
    {
        raceFolder_ = context.projectDir;
        race_->setFolder(raceFolder_);
    }
    loglik_->setEnabled(!context.file.isEmpty() && context.report.hasPosterior);

    table_->setSortingEnabled(false);
    table_->setRowCount(0);
    QStringList files = context.projectFiles;
    if (files.isEmpty() && !context.file.isEmpty())
    {
        files << context.file;
    }

    const auto number = [](const std::optional<double> &v) {
        auto *item = new QTableWidgetItem;
        if (v)
        {
            item->setData(Qt::DisplayRole, QString::number(*v, 'f', 2));
            item->setData(Qt::UserRole, *v);
            item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        }
        else
        {
            item->setText(QStringLiteral("—"));
            item->setTextAlignment(Qt::AlignCenter);
        }
        return item;
    };

    std::optional<double> best;
    int bestRow = -1;
    for (const QString &file : files)
    {
        const Criteria c = computeCriteria(file);
        const CheckReport report = file == context.file ? context.report : ModelRunner::check(file, {});
        const int row = table_->rowCount();
        table_->insertRow(row);
        auto *name = new QTableWidgetItem(QFileInfo(file).completeBaseName());
        name->setToolTip(file + (c.note.isEmpty() ? QString() : "\n" + c.note));
        if (file == context.file)
        {
            QFont bold = name->font();
            bold.setBold(true);
            name->setFont(bold);
        }
        table_->setItem(row, 0, name);
        table_->setItem(row, 1, new QTableWidgetItem(report.algorithm));
        table_->setItem(row, 2, number(c.periods > 0 ? std::optional<double>(c.periods) : std::nullopt));
        table_->item(row, 2)->setText(c.periods > 0 ? QString::number(c.periods) : QStringLiteral("—"));
        table_->setItem(row, 3, number(c.lppd));
        table_->setItem(row, 4, number(c.pWaic));
        table_->setItem(row, 5, number(c.waic));
        table_->setItem(row, 6, number(c.predictiveScore));
        if (c.waic && (!best || *c.waic < *best))
        {
            best = c.waic;
            bestRow = row;
        }
    }
    if (bestRow >= 0 && table_->rowCount() > 1)
    {
        table_->item(bestRow, 5)->setText(table_->item(bestRow, 5)->text() + QStringLiteral("  ★"));
        table_->item(bestRow, 5)->setToolTip(tr("Smallest WAIC in the project"));
    }
    table_->setSortingEnabled(true);
    setTableExportInfo(table_, tr("Model comparison"),
                       tr("WAIC = −2 (lppd − p_WAIC), smaller is better; the log predictive score sums the log "
                          "predictive density of the held-out periods, larger is better. ★: the smallest WAIC."));
    table_->resizeColumnsToContents();
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
}

// --------------------------------------------------------------------------
// 5. Application

ApplicationPage::ApplicationPage(QWidget *parent) : StagePage(parent)
{
    auto *layout = pageLayout(this);
    layout->addWidget(pageTitle(tr("Application")));

    // Forecasting
    auto *forecasting = new QWidget;
    auto *fl = new QVBoxLayout(forecasting);
    fl->setContentsMargins(0, 8, 0, 0);
    forecast_ = new QPushButton(tr("Run Forecast"));
    forecast_->setToolTip(tr("Draws the forecast of the selected model over the horizon set in its file."));
    connect(forecast_, &QPushButton::clicked, this, [this]() { emit runRequested({Stage::Forecast}); });
    variable_ = new QComboBox;
    connect(variable_, &QComboBox::currentIndexChanged, this, &ApplicationPage::showVariable);
    variable_->setToolTip(tr("The variable whose forecast is shown."));
    auto *bar = new QHBoxLayout;
    bar->addWidget(forecast_);
    bar->addSpacing(16);
    bar->addWidget(new QLabel(tr("Variable")));
    bar->addWidget(variable_);
    bar->addStretch(1);
    fl->addLayout(bar);
    forecastNote_ = subtle();
    fl->addWidget(forecastNote_);
    fan_ = new SeriesPlot(tr("Period"), tr("Value"));
    fl->addWidget(fan_, 1);

    // Structural analysis
    structural_ = new StructuralView;
    connect(structural_, &StructuralView::modelCreated, this, &StagePage::modelCreated);

    tabs_ = new QTabWidget;
    tabs_->setDocumentMode(true);
    tabs_->addTab(forecasting, tr("Forecasting"));
    tabs_->addTab(structural_, tr("Structural analysis"));
    spillover_ = new SpilloverView;
    tabs_->addTab(spillover_, tr("Spillovers"));
    multipliers_ = new MultipliersView;
    tabs_->addTab(multipliers_, tr("Multipliers"));
    historical_ = new HistoricalDecompositionView;
    tabs_->addTab(historical_, tr("Historical decomposition"));
    tabs_->setObjectName(QStringLiteral("applicationTabs"));
    layout->addWidget(tabs_, 1);
}

void ApplicationPage::showOnly(Panel panel)
{
    if (auto *title = findChild<QLabel *>(QStringLiteral("pageTitle")))
    {
        title->hide();
    }
    tabs_->tabBar()->hide();
    layout()->setContentsMargins(8, 6, 8, 6);
    tabs_->setCurrentIndex(panel == Panel::Forecasting ? 0 : 1);
}

void ApplicationPage::showStructural()
{
    tabs_->setCurrentWidget(structural_);
}

void ApplicationPage::showModel(const ModelContext &context)
{
    context_ = context;
    const CheckReport &r = context.report;
    forecast_->setEnabled(!context.file.isEmpty() && r.hasPosterior && r.horizon > 0);
    structural_->setModel(r.hasPosterior ? context.file : QString());
    spillover_->setModel(r.hasPosterior ? context.file : QString());
    multipliers_->setModel(r.hasPosterior ? context.file : QString());
    historical_->setModel(r.hasPosterior ? context.file : QString());

    const QSignalBlocker block(variable_);
    variable_->clear();
    const QStringList names = context.file.isEmpty() ? QStringList() : levelNames(context.file);
    for (int i = 0; i < r.k; ++i)
    {
        variable_->addItem(i < names.size() ? names[i] : tr("Variable %1").arg(i + 1));
    }
    showVariable(0);
}

void ApplicationPage::showVariable(int variable)
{
    fan_->clear();
    const CheckReport &r = context_.report;
    if (context_.file.isEmpty() || variable < 0 || r.k <= 0)
    {
        forecastNote_->setText(tr("Select a model of the project."));
        return;
    }
    if (r.horizon <= 0)
    {
        forecastNote_->setText(tr("This model has no forecast horizon (/model/h)."));
        return;
    }
    const H5Node *node =
        context_.tree ? context_.tree->find(QStringLiteral("/posterior/forecast/forecasts")) : nullptr;
    if (node == nullptr)
    {
        forecastNote_->setText(tr("No forecast yet. Estimate the model, then run the forecast."));
        return;
    }
    try
    {
        const bool vec = r.algorithm.startsWith(QLatin1String("Vec"));
        std::vector<double> y;
        if (vec)
        {
            // A VEC is estimated in differences and forecast in levels. Its
            // levels are in the file all the same: column t of /data/train/w
            // starts with y_{t-1}, and the first lag block of
            // /data/forecast/x is y_T, which together are the history from
            // the period before the sample to its end.
            const Matrix w = readMatrix(context_.file, QStringLiteral("/data/train/w"));   // (k_beta, tt)
            const Matrix fx = readMatrix(context_.file, QStringLiteral("/data/forecast/x"));   // (n_x, h)
            const auto row = static_cast<std::size_t>(variable);
            for (std::size_t t = 0; t < w.cols; ++t)
            {
                y.push_back(w.at(row, t));
            }
            y.push_back(fx.at(row, 0));
        }
        else
        {
            y = seriesOf(context_.file, r.k)[static_cast<std::size_t>(variable)];
        }
        const std::size_t shown = std::min<std::size_t>(y.size(), static_cast<std::size_t>(4 * r.horizon));
        const std::vector<double> tail(y.end() - static_cast<std::ptrdiff_t>(shown), y.end());

        // Forecasts are stacked by period like the data: row h*k + i.
        const Matrix f = readMatrix(context_.file, QStringLiteral("/posterior/forecast/forecasts"));
        const std::size_t horizon = f.rows / static_cast<std::size_t>(r.k);
        std::vector<std::vector<double>> draws(horizon, std::vector<double>(f.cols));
        for (std::size_t h = 0; h < horizon; ++h)
        {
            for (std::size_t d = 0; d < f.cols; ++d)
            {
                draws[h][d] = f.at(h * static_cast<std::size_t>(r.k) + static_cast<std::size_t>(variable), d);
            }
        }
        // The history ends at the last sample period, x = y.size() - 1 for a
        // VEC (whose history starts one period before the sample) and
        // y.size() for a VAR; the forecast takes over from there.
        const double last = vec ? static_cast<double>(y.size() - 1) : static_cast<double>(y.size());
        fan_->addLine(tr("Observed"), tail, 0, last - static_cast<double>(shown));
        fan_->addFan(tr("Forecast median"), draws, 1, last, y.back());
        forecastNote_->setText(tr("%1-step forecast from %2 draws; median with 68% and 90% bands. "
                                  "VEC forecasts are in levels.")
                                   .arg(horizon)
                                   .arg(f.cols));
    }
    catch (const std::exception &e)
    {
        forecastNote_->setText(tr("The forecast cannot be shown: %1").arg(QString::fromUtf8(e.what())));
    }
    fan_->replot();
}

} // namespace studio
