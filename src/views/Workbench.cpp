// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Franz X. Mohr

#include "views/Workbench.h"

#include "views/BuilderDialog.h"
#include "views/SeriesPlot.h"

#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QVBoxLayout>

namespace studio
{

namespace
{

/// A column of the workbench: its title over its content.
QWidget *column(const QString &title, QWidget *content)
{
    auto *panel = new QWidget;
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(6, 8, 6, 0);
    layout->setSpacing(4);
    auto *label = new QLabel(title);
    label->setObjectName(QStringLiteral("sectionTitle"));
    layout->addWidget(label);
    layout->addWidget(content, 1);
    return panel;
}

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

    auto *left = new QSplitter(Qt::Vertical);
    left->addWidget(column(tr("Modifications"), modScroll));
    left->addWidget(column(tr("Data"), seriesPanel));
    left->setStretchFactor(0, 3);
    left->setStretchFactor(1, 2);
    left->setChildrenCollapsible(false);

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
    auto *right = new QSplitter(Qt::Vertical);
    right->addWidget(column(tr("Impulse responses"), responses_));
    right->addWidget(column(tr("Forecasts"), forecasts_));
    right->setChildrenCollapsible(false);

    // The builder's forms need their width: below it their labels clip.
    left->setMinimumWidth(440);
    QWidget *priorColumn = column(tr("Priors"), priorScroll);
    priorColumn->setMinimumWidth(440);

    auto *split = new QSplitter(Qt::Horizontal);
    split->addWidget(left);
    split->addWidget(priorColumn);
    split->addWidget(column(tr("Posterior draws"), posterior_));
    split->addWidget(right);
    split->setChildrenCollapsible(false);
    // A wide screen: the two specification columns narrow, the results wide.
    split->setStretchFactor(0, 2);
    split->setStretchFactor(1, 2);
    split->setStretchFactor(2, 4);
    split->setStretchFactor(3, 4);
    split->setSizes({440, 440, 760, 760});

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(split);

    drawSeries();
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
