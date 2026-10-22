// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Franz X. Mohr

#include "views/Workbench.h"

#include "views/BuilderDialog.h"
#include "engine/FactorAnalysis.h"
#include "engine/ModelFileTree.h"
#include "views/FoldablePanes.h"
#include "views/SeriesPlot.h"

#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QGuiApplication>
#include <QHash>
#include <QScreen>
#include <QSettings>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QVBoxLayout>

#include <algorithm>

namespace studio
{

namespace
{

/// A scrolling stack the builder's sections are put in.
QScrollArea *scrolling(QVBoxLayout *&inner)
{
    auto *body = new QWidget;
    inner = new QVBoxLayout(body);
    inner->setContentsMargins(0, 0, 4, 0);
    inner->addStretch(1);
    auto *scroll = new QScrollArea;
    scroll->setWidget(body);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    return scroll;
}

/// Before the stretch that closes a scrolling stack.
void insert(QVBoxLayout *layout, QWidget *widget)
{
    layout->insertWidget(layout->count() - 1, widget);
}

} // namespace

Workbench::Workbench(QtReporter *reporter, QWidget *parent) : QWidget(parent)
{
    // 1. Modifications, over the series.
    QScrollArea *modScroll = scrolling(modifications_);
    noData_ = new QWidget;
    {
        auto *l = new QVBoxLayout(noData_);
        auto *text = new QLabel(tr("Import a data set to specify a model here: the variables and the sample, the "
                                   "lags and deterministic terms, constant or time-varying coefficients, the "
                                   "error variance and the chain. Each estimate joins the project as a model "
                                   "of its own."));
        text->setWordWrap(true);
        auto *import = new QPushButton(tr("Import Data…"));
        import->setToolTip(tr("A delimited file of time series, one column per series, to specify models on."));
        connect(import, &QPushButton::clicked, this, &Workbench::importRequested);
        l->addWidget(text);
        l->addWidget(import, 0, Qt::AlignLeft);
    }
    insert(modifications_, noData_);
    source_ = new QLabel;
    source_->setObjectName(QStringLiteral("subtle"));
    source_->setWordWrap(true);
    insert(modifications_, source_);

    auto *seriesBody = new QWidget;
    seriesLayout_ = new QVBoxLayout(seriesBody);
    seriesLayout_->setContentsMargins(0, 0, 4, 0);
    seriesNote_ = new QLabel;
    seriesNote_->setObjectName(QStringLiteral("subtle"));
    seriesNote_->setWordWrap(true);
    auto *seriesScroll = new QScrollArea;
    seriesScroll->setWidget(seriesBody);
    seriesScroll->setWidgetResizable(true);
    seriesScroll->setFrameShape(QFrame::NoFrame);
    seriesScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *seriesPanel = new QWidget;
    {
        auto *l = new QVBoxLayout(seriesPanel);
        l->setContentsMargins(0, 0, 0, 0);
        l->addWidget(seriesNote_);
        l->addWidget(seriesScroll, 1);
    }

    auto *modPane = new FoldPane(QStringLiteral("modifications"), tr("Modifications"), modScroll);
    auto *dataPane = new FoldPane(QStringLiteral("data"), tr("Data"), seriesPanel);

    // 2. Priors, and what BayesTS says about the model so specified.
    QScrollArea *priorScroll = scrolling(priors_);
    priorsHint_ = new QLabel(tr("The prior of the model specified on the left: its coefficients, its error "
                                "variance and, for time-varying coefficients, their state variance."));
    priorsHint_->setObjectName(QStringLiteral("subtle"));
    priorsHint_->setWordWrap(true);
    insert(priors_, priorsHint_);

    // 3. Posterior draws of the selected model.
    posterior_ = new PosteriorPage(reporter);
    posterior_->setCompact();
    connect(posterior_, &StagePage::runRequested, this, &Workbench::runRequested);

    // 4. Impulse responses above forecasts.
    responses_ = new ApplicationPage;
    responses_->showOnly(ApplicationPage::Panel::Structural);
    connect(responses_, &StagePage::modelCreated, this, &Workbench::modelCreated);
    forecasts_ = new ApplicationPage;
    forecasts_->showOnly(ApplicationPage::Panel::Forecasting);
    connect(forecasts_, &StagePage::runRequested, this, &Workbench::runRequested);
    auto *responsesPane = new FoldPane(QStringLiteral("responses"), tr("Impulse responses"), responses_);
    // Its rows of panels at a height that reads: the decomposition below
    // takes the rest of the column.
    connect(responses_, &ApplicationPage::naturalHeightChanged, this, [this, responsesPane](int height) {
        responsesPane->setContentLimit(height);
        relayoutColumns();
    });
    auto *forecastsPane = new FoldPane(QStringLiteral("forecasts"), tr("Forecasts"), forecasts_);
    // The fan at a height that reads, or the note alone without a forecast.
    connect(forecasts_, &ApplicationPage::naturalHeightChanged, this, [this, forecastsPane](int height) {
        forecastsPane->setContentLimit(height);
        relayoutColumns();
    });
    historical_ = new ApplicationPage;
    historical_->showOnly(ApplicationPage::Panel::Historical);
    auto *historicalPane =
        new FoldPane(QStringLiteral("historical"), tr("Historical decomposition"), historical_);
    spillovers_ = new ApplicationPage;
    spillovers_->showOnly(ApplicationPage::Panel::Spillover);
    auto *spilloverPane = new FoldPane(QStringLiteral("spillovers"), tr("Spillovers"), spillovers_);
    // A table's tab needs its rows, the plots' all they get: the pane fits
    // the tab showing, and the multipliers and forecasts take the rest.
    connect(spillovers_, &ApplicationPage::naturalHeightChanged, this, [this, spilloverPane](int height) {
        spilloverPane->setContentLimit(height);
        relayoutColumns();
    });
    multipliers_ = new ApplicationPage;
    multipliers_->showOnly(ApplicationPage::Panel::Multipliers);
    auto *multiplierPane = new FoldPane(QStringLiteral("multipliers"), tr("Multipliers"), multipliers_);
    // Its panels at a height that reads, or its note alone: the rest of the
    // column to the forecasts.
    connect(multipliers_, &ApplicationPage::naturalHeightChanged, this, [this, multiplierPane](int height) {
        multiplierPane->setContentLimit(height);
        relayoutColumns();
    });
    // A factor model's and an unobserved components model's analyses, in the
    // column of the VAR's structural analysis: a model has one or the other.
    factors_ = new ApplicationPage;
    factors_->showOnly(ApplicationPage::Panel::Factors);
    auto *factorsPane = new FoldPane(QStringLiteral("factors"), tr("Factors"), factors_);
    trendCycle_ = new ApplicationPage;
    trendCycle_->showOnly(ApplicationPage::Panel::TrendCycle);
    auto *trendCyclePane = new FoldPane(QStringLiteral("trendcycle"), tr("Trend and cycle"), trendCycle_);
    // Settings only, never draws: the state variance view, whose runs are
    // posteriors, stays a tab of the posterior column.
    auto *priorPane = new FoldPane(QStringLiteral("priors"), tr("Priors"), priorScroll);
    auto *posteriorPane = new FoldPane(QStringLiteral("posterior"), tr("Posterior draws"), posterior_);

    // A wide screen: the two specification columns narrow -- the builder's
    // forms need 440 pixels, below which their labels clip -- the results
    // wide. Each pane folds; a column whose panes are all folded becomes a
    // strip, so a laptop shows one or two panes at a time.
    split_ = new QSplitter(Qt::Horizontal);
    split_->setChildrenCollapsible(false);
    // The application in two columns: the structural analysis -- responses
    // and the historical decomposition -- and then spillovers, multipliers
    // and forecasts.
    columns_ = {new FoldColumn({modPane, dataPane}, 2, 440, {3, 2}), new FoldColumn({priorPane}, 2, 440, {1}),
                new FoldColumn({posteriorPane}, 3, 0, {1}),
                new FoldColumn({responsesPane, historicalPane, factorsPane, trendCyclePane}, 3, 0, {1, 1, 2, 2}),
                new FoldColumn({spilloverPane, multiplierPane, forecastsPane}, 3, 0, {1, 1, 1})};
    for (FoldColumn *c : columns_)
    {
        split_->addWidget(c);
        connect(c, &FoldColumn::changed, this, [this]() {
            arrangeColumns();
            saveFolds();
        });
        for (FoldPane *pane : c->panes())
        {
            panes_.push_back(pane);
            connect(pane, &FoldPane::focusRequested, this, [this, pane]() { focusPane(pane); });
            // Folded or unfolded by hand: the user's, from now on.
            connect(pane, &FoldPane::foldToggled, this, [this, pane]() { autoFolded_.remove(pane->id()); });
        }
    }
    restoreFolds();
    foldForModel(QString());

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(split_);

    drawSeries();
}

void Workbench::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (!arranged_)
    {
        arranged_ = true;
        relayoutColumns();
    }
}

void Workbench::relayoutColumns()
{
    for (FoldColumn *c : columns_)
    {
        c->relayout();
    }
    // Folded columns their strip; the others the rest by weight, each at
    // least what it needs.
    const int handles = split_->handleWidth() * static_cast<int>(columns_.size() - 1);
    int free = std::max(split_->width(), 1200) - handles;
    int weights = 0;
    for (const FoldColumn *c : columns_)
    {
        if (c->allFolded())
        {
            free -= c->stripWidth();
        }
        else
        {
            weights += c->weight();
        }
    }
    QList<int> sizes;
    for (const FoldColumn *c : columns_)
    {
        sizes << (c->allFolded() ? c->stripWidth()
                                 : std::max(c->unfoldedMinimum(), free * c->weight() / std::max(1, weights)));
    }
    split_->setSizes(sizes);
}

void Workbench::arrangeColumns()
{
    relayoutColumns();
}

void Workbench::focusPane(FoldPane *pane)
{
    // Alone already: unfold them all again. Otherwise fold the others.
    const bool alone = std::none_of(panes_.begin(), panes_.end(),
                                    [pane](const FoldPane *p) { return p != pane && !p->isFolded(); });
    for (FoldPane *p : panes_)
    {
        p->setFolded(alone ? false : p != pane);
    }
    relayoutColumns();
    saveFolds();
}

void Workbench::unfoldAll()
{
    for (FoldPane *p : panes_)
    {
        p->setFolded(false);
    }
    relayoutColumns();
    saveFolds();
}

void Workbench::setFoldedPanes(const QStringList &ids)
{
    for (FoldPane *p : panes_)
    {
        p->setFolded(ids.contains(p->id()));
    }
    relayoutColumns();
    saveFolds();
}

void Workbench::foldForModel(const QString &file)
{
    // Once per model: a run ending refreshes the same one, and a pane the
    // user folded or unfolded stays so.
    if (foldsDecided_ && file == foldedFor_)
    {
        return;
    }
    foldsDecided_ = true;
    foldedFor_ = file;
    // No model yet counts as a VAR without exogenous variables: the factor
    // and UC panes folded until a model needs them.
    const QString algorithm = file.isEmpty() ? QString() : modelAlgorithm(file);
    const bool factor = algorithm.startsWith(QLatin1String("Dfm")) || algorithm.startsWith(QLatin1String("Favar"));
    const bool uc = algorithm.startsWith(QLatin1String("Uc"));
    const bool var = !factor && !uc;
    // Dynamic multipliers need weakly exogenous variables, /model/m of them.
    const bool exogenous = !file.isEmpty() && readGroupInt(file, QStringLiteral("/model"), QStringLiteral("m"), 0) > 0;
    const QHash<QString, bool> applies = {
        {QStringLiteral("responses"), var},  {QStringLiteral("historical"), var},
        {QStringLiteral("spillovers"), var}, {QStringLiteral("multipliers"), var && exogenous},
        {QStringLiteral("factors"), factor}, {QStringLiteral("trendcycle"), uc},
    };
    bool changed = false;
    for (FoldPane *pane : panes_)
    {
        const auto it = applies.constFind(pane->id());
        if (it == applies.constEnd())
        {
            continue;
        }
        if (!it.value() && !pane->isFolded())
        {
            pane->setFolded(true);
            autoFolded_.insert(pane->id());
            changed = true;
        }
        else if (it.value() && autoFolded_.contains(pane->id()))
        {
            autoFolded_.remove(pane->id());
            if (pane->isFolded())
            {
                pane->setFolded(false);
                changed = true;
            }
        }
    }
    if (changed)
    {
        relayoutColumns();
    }
}

void Workbench::saveFolds()
{
    QStringList folded;
    for (const FoldPane *p : panes_)
    {
        if (p->isFolded() && !autoFolded_.contains(p->id()))
        {
            folded << p->id();
        }
    }
    QSettings().setValue(QStringLiteral("workbench/folded"), folded);
}

void Workbench::restoreFolds()
{
    QSettings settings;
    QStringList folded;
    if (settings.contains(QStringLiteral("workbench/folded")))
    {
        folded = settings.value(QStringLiteral("workbench/folded")).toStringList();
    }
    else if (const QScreen *screen = QGuiApplication::primaryScreen();
             screen != nullptr && QGuiApplication::platformName() != QLatin1String("offscreen") &&
             screen->availableGeometry().width() < 1900)
    {
        // A first start on a laptop: the specification and the draws; the
        // priors and the application a click away.
        folded = {QStringLiteral("priors"), QStringLiteral("responses"), QStringLiteral("historical"),
                  QStringLiteral("spillovers"), QStringLiteral("multipliers"), QStringLiteral("forecasts")};
    }
    for (FoldPane *p : panes_)
    {
        p->setFolded(folded.contains(p->id()));
    }
    for (FoldColumn *c : columns_)
    {
        c->relayout();
    }
}

void Workbench::setData(const DataSet &data)
{
    imported_ = data;
    formModel_.clear();
    rebuildBuilder(imported_, std::nullopt);
    drawSeries();
}

void Workbench::rebuildBuilder(const DataSet &data, const std::optional<VarBuilderSpec> &spec)
{
    data_ = data;
    for (QWidget *w : sections_)
    {
        delete w;
    }
    sections_.clear();
    delete builder_;
    builder_ = nullptr;
    noData_->setVisible(data_.empty());
    priorsHint_->setVisible(true);
    if (data_.empty())
    {
        return;
    }
    builder_ = new BuilderDialog(data_, projectDir_, this, BuilderDialog::Mode::Panels);
    if (spec)
    {
        builder_->setSpec(*spec);
    }
    builder_->hide();
    connect(builder_, &BuilderDialog::createRequested, this, &Workbench::specify);
    using S = BuilderDialog::Section;
    for (const S s : {S::Data, S::Specification, S::Sampler})
    {
        QWidget *w = builder_->takeSection(s);
        insert(modifications_, w);
        sections_.push_back(w);
    }
    for (const S s : {S::Priors, S::Summary})
    {
        QWidget *w = builder_->takeSection(s);
        insert(priors_, w);
        sections_.push_back(w);
    }
}

void Workbench::drawSeries()
{
    while (QLayoutItem *item = seriesLayout_->takeAt(0))
    {
        delete item->widget();
        delete item;
    }
    std::vector<std::vector<double>> series;
    QStringList names;
    if (!data_.empty())
    {
        for (int c = 0; c < data_.series(); ++c)
        {
            const arma::vec col = data_.values.col(static_cast<arma::uword>(c));
            series.emplace_back(col.begin(), col.end());
        }
        names = data_.names;
        seriesNote_->setText(tr("%1: %2 periods, %3 to %4.")
                                 .arg(QFileInfo(data_.source).fileName())
                                 .arg(data_.length())
                                 .arg(data_.periodLabel(0), data_.periodLabel(data_.length() - 1)));
    }
    else if (!context_.file.isEmpty() && context_.report.k > 0)
    {
        try
        {
            series = sampleSeries(context_.file, context_.report.k);
            names = sampleNames(context_.file);
            seriesNote_->setText(tr("The estimation sample of the selected model."));
        }
        catch (const std::exception &e)
        {
            seriesNote_->setText(tr("The data cannot be shown: %1").arg(QString::fromUtf8(e.what())));
        }
    }
    else
    {
        seriesNote_->setText(tr("No data yet."));
    }
    for (std::size_t i = 0; i < series.size(); ++i)
    {
        const auto n = static_cast<qsizetype>(i);
        const QString name = n < names.size() ? names[n] : tr("Variable %1").arg(i + 1);
        auto *plot = new SeriesPlot(QString(), name);
        plot->hideLegend();
        plot->setExportTitle(name);
        plot->setMinimumHeight(130);
        plot->addLine(name, series[i], static_cast<int>(i));
        plot->replot();
        seriesLayout_->addWidget(plot);
    }
    seriesLayout_->addStretch(1);
}

void Workbench::showModel(const ModelContext &context)
{
    context_ = context;
    posterior_->showModel(context);
    responses_->showModel(context);
    forecasts_->showModel(context);
    foldForModel(context.file);
    historical_->showModel(context);
    spillovers_->showModel(context);
    multipliers_->showModel(context);
    factors_->showModel(context);
    trendCycle_->showModel(context);

    // The form follows the selected model when the builder wrote it: its own
    // data, its own settings. Only on a change of model -- a run ending
    // refreshes the same one, and must not undo edits in progress.
    if (context.file != selected_)
    {
        selected_ = context.file;
        if (std::optional<VarBuilderSpec> spec = context.file.isEmpty() ? std::nullopt
                                                                        : readVarModelSpec(context.file))
        {
            formModel_ = context.file;
            const DataSet data = spec->data;
            rebuildBuilder(data, spec);
        }
        else if (!formModel_.isEmpty())
        {
            // From another model's settings back to the imported data, if any.
            formModel_.clear();
            rebuildBuilder(imported_, std::nullopt);
        }
    }
    const QString name = QFileInfo(context.file).fileName();
    source_->setText(!formModel_.isEmpty()
                         ? tr("Filled in from %1. Change what you like and estimate it as a new model; "
                              "%1 itself stays as it is.").arg(name)
                     : context.file.isEmpty() || data_.empty()
                         ? (context.file.isEmpty() ? QString()
                                                   : tr("%1 was not written by the studio's model builder, or "
                                                        "written before it recorded its settings, so they cannot "
                                                        "be filled in here.").arg(name))
                         : tr("The builder's defaults on the imported data: %1 was not written by the studio's "
                              "model builder, or written before it recorded its settings.").arg(name));
    source_->setVisible(!source_->text().isEmpty());
    drawSeries();
}

void Workbench::specify()
{
    if (builder_ == nullptr)
    {
        return;
    }
    const QString dir = !projectDir_.isEmpty() ? projectDir_ : QFileInfo(data_.source).absolutePath();
    const QFileInfo suggested(QDir(dir).filePath(builder_->suggestedName()));
    QString path = suggested.absoluteFilePath();
    for (int n = 2; QFileInfo::exists(path); ++n)
    {
        path = QDir(dir).filePath(QStringLiteral("%1-%2.%3").arg(suggested.completeBaseName()).arg(n).arg(
            suggested.suffix().isEmpty() ? QStringLiteral("h5") : suggested.suffix()));
    }
    if (builder_->createAt(path))
    {
        emit modelSpecified(path);
    }
}

} // namespace studio
