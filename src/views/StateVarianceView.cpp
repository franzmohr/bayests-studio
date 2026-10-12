// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Franz X. Mohr

#include "views/StateVarianceView.h"

#include "engine/BatchRunner.h"
#include "views/FigureExport.h"
#include "views/PlotStyle.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleValidator>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPen>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QVBoxLayout>

#include <qwt_interval.h>
#include <qwt_legend.h>
#include <qwt_plot.h>
#include <qwt_plot_curve.h>
#include <qwt_plot_intervalcurve.h>
#include <qwt_samples.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace studio
{

namespace
{

QString number(double v)
{
    return QString::number(v, 'g', 3);
}

double quantileOfSorted(const std::vector<double> &sorted, double q)
{
    const double pos = q * static_cast<double>(sorted.size() - 1);
    const auto lo = static_cast<std::size_t>(std::floor(pos));
    const auto hi = std::min(lo + 1, sorted.size() - 1);
    return sorted[lo] + (pos - static_cast<double>(lo)) * (sorted[hi] - sorted[lo]);
}

QTableWidgetItem *cell(const QString &text, Qt::Alignment align = Qt::AlignRight | Qt::AlignVCenter)
{
    auto *item = new QTableWidgetItem(text);
    item->setTextAlignment(align);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    return item;
}

} // namespace

StateVarianceView::StateVarianceView(QWidget *parent) : QWidget(parent)
{
    runner_ = new BatchRunner(this);
    connect(runner_, &BatchRunner::failed, this, [this](const QString &file, const QString &message) {
        for (Run &r : runs_)
        {
            if (r.file == file)
            {
                r.error = message;
            }
        }
    });
    connect(runner_, &BatchRunner::progress, this, [this](int done, int total, const QString &) {
        status_->setText(tr("Estimating — %1 of %2 runs done").arg(done).arg(total));
    });
    connect(runner_, &BatchRunner::finished, this, [this](int, int, bool) { batchFinished(); });

    intro_ = new QLabel(tr("How much should a coefficient be let drift? Name a state variance — the variance of "
                           "each period's step in a coefficient's random walk — and the model is estimated again "
                           "with it, its path drawn over the others. A small variance flattens the path towards a "
                           "constant; a large one lets it chase the noise. Your model file is not changed."));
    intro_->setWordWrap(true);

    coefficient_ = new QComboBox;
    coefficient_->setMinimumContentsLength(24);
    coefficient_->setMaxVisibleItems(24);
    coefficient_->setToolTip(tr("The coefficient whose path is compared across the runs."));
    connect(coefficient_, &QComboBox::currentIndexChanged, this, [this]() { redraw(); });

    variance_ = new QLineEdit;
    auto *validator = new QDoubleValidator(0.0, std::numeric_limits<double>::max(), 12, variance_);
    validator->setNotation(QDoubleValidator::ScientificNotation);
    variance_->setValidator(validator);
    variance_->setMaximumWidth(120);
    variance_->setToolTip(tr("The state variance of a typical coefficient. Every coefficient's prior is scaled by "
                             "the same factor, so the constant and the trend keep their ratio to it."));
    connect(variance_, &QLineEdit::textChanged, this, [this]() { updateControls(); });

    fixed_ = new QCheckBox(tr("Hold it fixed"));
    fixed_->setToolTip(tr("Makes the prior so tight that the variance is set rather than estimated: what the path "
                          "looks like at exactly this variance. Unticked, the prior is only centred there and the "
                          "data may move it, as in your model."));

    iterations_ = new QSpinBox;
    iterations_->setRange(50, 1000000);
    iterations_->setSingleStep(100);
    iterations_->setToolTip(tr("Kept draws per run. Fewer than your model's make a quicker, rougher picture."));
    burnin_ = new QSpinBox;
    burnin_->setRange(0, 1000000);
    burnin_->setSingleStep(100);
    burnin_->setToolTip(tr("Burn-in draws per run."));

    run_ = new QPushButton(tr("Estimate"));
    run_->setDefault(true);
    run_->setToolTip(tr("Estimates the model again with this state variance."));
    connect(run_, &QPushButton::clicked, this, [this]() { start({variance_->text().toDouble()}); });
    ladder_ = new QPushButton(tr("Estimate ÷10, ×1, ×10"));
    ladder_->setToolTip(tr("Three runs at once: a tenth of this variance, this variance and ten times it."));
    connect(ladder_, &QPushButton::clicked, this, [this]() {
        const double v = variance_->text().toDouble();
        start({v / 10.0, v, v * 10.0});
    });
    clear_ = new QPushButton(tr("Clear Runs"));
    connect(clear_, &QPushButton::clicked, this, [this]() {
        clearRuns();
        redraw();
    });

    auto *controls = new QHBoxLayout;
    controls->addWidget(new QLabel(tr("Coefficient")));
    controls->addWidget(coefficient_);
    controls->addSpacing(12);
    controls->addWidget(new QLabel(tr("State variance")));
    controls->addWidget(variance_);
    controls->addWidget(fixed_);
    controls->addSpacing(12);
    controls->addWidget(new QLabel(tr("Draws")));
    controls->addWidget(iterations_);
    controls->addWidget(new QLabel(tr("Burn-in")));
    controls->addWidget(burnin_);
    controls->addStretch(1);

    auto *buttons = new QHBoxLayout;
    status_ = new QLabel;
    status_->setObjectName(QStringLiteral("subtle"));
    status_->setWordWrap(true);
    buttons->addWidget(status_, 1);
    buttons->addWidget(clear_);
    buttons->addWidget(ladder_);
    buttons->addWidget(run_);

    plot_ = new QwtPlot;
    styleAxes(plot_, tr("Period"), tr("Coefficient"));
    plot_->insertLegend(new QwtLegend, QwtPlot::BottomLegend);
    plot_->setToolTip(tr("Each run's median path with its 68% band. Right-click to copy or save the figure."));
    attachFigureExport(plot_, [this]() {
        return singleFigure(plot_, tr("State variance and the path of %1").arg(coefficient_->currentText()),
                            QStringLiteral("state-variance-%1").arg(coefficient_->currentText()));
    });

    table_ = new QTableWidget(0, 5);
    table_->setHorizontalHeaderLabels({tr("Run"), tr("Prior state variance"), tr("Prior"),
                                       tr("Posterior state s.d. [68%]"), tr("Median path moves by")});
    table_->setToolTip(tr("Per run, for the chosen coefficient: the state variance its prior is at, and the "
                          "standard deviation of each step after seeing the data. Where the two agree whatever "
                          "the prior, the data decide; where the posterior follows the prior, the prior does. "
                          "The last column is the range of the median path over the sample."));
    table_->verticalHeader()->setVisible(false);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setSelectionMode(QAbstractItemView::NoSelection);

    auto *split = new QSplitter(Qt::Vertical);
    split->addWidget(plot_);
    split->addWidget(table_);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 1);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(intro_);
    layout->addLayout(controls);
    layout->addLayout(buttons);
    layout->addWidget(split, 1);

    setModel(QString(), {});
}

StateVarianceView::~StateVarianceView()
{
    runner_->cancel();
}

void StateVarianceView::setModel(const QString &file, const QStringList &names)
{
    if (file == file_ && !file.isEmpty())
    {
        // The same model again -- after its own run, say: the reference is
        // read from the file at every redraw.
        redraw();
        return;
    }
    runner_->cancel();
    clearRuns();
    file_ = file;
    names_ = names;
    prior_ = file.isEmpty() ? StateVariancePrior{} : readStateVariancePrior(file);

    {
        const QSignalBlocker block(coefficient_);
        coefficient_->clear();
        for (int i = 0; i < prior_.coefficients; ++i)
        {
            coefficient_->addItem(i < names.size() ? names[i] : tr("Coefficient %1").arg(i + 1));
        }
    }
    if (prior_.available)
    {
        variance_->setText(number(prior_.typical));
        fixed_->setEnabled(!prior_.nonCentred);
        fixed_->setChecked(false);
        iterations_->setValue(std::min(std::max(prior_.iterations, 50), 1000));
        burnin_->setValue(std::min(prior_.burnin, 1000));
        // The model's own posterior, where it has one, is the reference.
        Run own{tr("Your model"), file, prior_.typical, false, true, true, {}};
        runs_.push_back(own);
    }
    redraw();
}

void StateVarianceView::clearRuns()
{
    runs_.erase(std::remove_if(runs_.begin(), runs_.end(), [](const Run &r) { return !r.reference; }), runs_.end());
    if (!runner_->isRunning())
    {
        folder_.reset();
    }
}

void StateVarianceView::start(const std::vector<double> &variances)
{
    if (!prior_.available || runner_->isRunning())
    {
        return;
    }
    if (!folder_)
    {
        folder_ = std::make_unique<QTemporaryDir>();
    }
    const QString base = QFileInfo(file_).completeBaseName();
    QStringList files;
    for (const double v : variances)
    {
        Run r;
        r.variance = v;
        r.fixed = fixed_->isChecked() && !prior_.nonCentred;
        r.label = r.fixed ? tr("Fixed at %1").arg(number(v)) : tr("Prior at %1").arg(number(v));
        r.file = folder_->filePath(QStringLiteral("%1-state-variance-%2.h5").arg(base).arg(++serial_));
        try
        {
            writeStateVarianceVariant(file_, r.file, {v, r.fixed, iterations_->value(), burnin_->value()});
        }
        catch (const std::exception &e)
        {
            status_->setText(tr("Cannot prepare the run: %1").arg(QString::fromUtf8(e.what())));
            return;
        }
        files << r.file;
        runs_.push_back(r);
    }
    status_->setText(tr("Estimating %n run(s)…", nullptr, static_cast<int>(files.size())));
    runner_->start(files);
    updateControls();
}

void StateVarianceView::batchFinished()
{
    for (Run &r : runs_)
    {
        if (!r.reference && !r.done && r.error.isEmpty())
        {
            r.done = true;
        }
    }
    redraw();
}

void StateVarianceView::updateControls()
{
    const bool busy = runner_->isRunning();
    const bool ok = prior_.available && variance_->text().toDouble() > 0.0;
    for (QWidget *w : std::initializer_list<QWidget *>{variance_, iterations_, burnin_, coefficient_})
    {
        w->setEnabled(prior_.available);
    }
    fixed_->setEnabled(prior_.available && !prior_.nonCentred);
    run_->setEnabled(ok && !busy);
    ladder_->setEnabled(ok && !busy);
    clear_->setEnabled(!busy && std::any_of(runs_.begin(), runs_.end(), [](const Run &r) { return !r.reference; }));
}

void StateVarianceView::redraw()
{
    plot_->detachItems(QwtPlotItem::Rtti_PlotCurve, true);
    plot_->detachItems(QwtPlotItem::Rtti_PlotIntervalCurve, true);
    table_->setRowCount(0);
    updateControls();

    if (!prior_.available)
    {
        status_->setText(file_.isEmpty() ? tr("Open a model with time-varying coefficients.") : prior_.refusal);
        plot_->replot();
        return;
    }
    if (!runner_->isRunning())
    {
        status_->setText(prior_.nonCentred
                             ? tr("Your model's prior puts a typical coefficient's state variance at %1 "
                                  "(non-centred: the prior variance of ω).").arg(number(prior_.typical))
                             : tr("Your model's prior puts a typical coefficient's state variance at %1.")
                                   .arg(number(prior_.typical)));
    }

    const int c = std::max(0, coefficient_->currentIndex());
    int colour = 0;
    for (const Run &r : runs_)
    {
        const int row = table_->rowCount();
        table_->insertRow(row);
        auto *name = cell(r.label, Qt::AlignLeft | Qt::AlignVCenter);
        name->setForeground(seriesColor(colour));
        table_->setItem(row, 0, name);
        table_->setItem(row, 1, cell(number(r.variance)));
        table_->setItem(row, 2, cell(r.reference ? tr("as in the file") : r.fixed ? tr("fixed") : tr("centred there"),
                                     Qt::AlignLeft | Qt::AlignVCenter));
        if (!r.error.isEmpty())
        {
            table_->setItem(row, 3, cell(r.error, Qt::AlignLeft | Qt::AlignVCenter));
            ++colour;
            continue;
        }
        if (!r.done)
        {
            table_->setItem(row, 3, cell(tr("running…"), Qt::AlignLeft | Qt::AlignVCenter));
            ++colour;
            continue;
        }
        StatePathSummary s;
        try
        {
            s = summariseStatePath(r.file, c, prior_.coefficients);
        }
        catch (const std::exception &)
        {
            table_->setItem(row, 3, cell(r.reference ? tr("not estimated yet") : tr("no draws"),
                                         Qt::AlignLeft | Qt::AlignVCenter));
            ++colour;
            continue;
        }
        if (!s.stateSd.empty())
        {
            table_->setItem(row, 3, cell(tr("%1 [%2, %3]")
                                             .arg(number(quantileOfSorted(s.stateSd, 0.5)),
                                                  number(quantileOfSorted(s.stateSd, 0.16)),
                                                  number(quantileOfSorted(s.stateSd, 0.84)))));
        }
        const auto [lo, hi] = std::minmax_element(s.median.begin(), s.median.end());
        table_->setItem(row, 4, cell(number(*hi - *lo)));

        QVector<QwtIntervalSample> band;
        QVector<QPointF> median;
        for (std::size_t t = 0; t < s.median.size(); ++t)
        {
            const double x = static_cast<double>(t + 1);
            band.append({x, QwtInterval(s.lo68[t], s.hi68[t])});
            median.append({x, s.median[t]});
        }
        auto *tube = new QwtPlotIntervalCurve(r.label);
        tube->setStyle(QwtPlotIntervalCurve::Tube);
        tube->setPen(QPen(Qt::transparent, 0.0));
        tube->setBrush(seriesColor(colour, 55));
        tube->setRenderHint(QwtPlotItem::RenderAntialiased);
        tube->setItemAttribute(QwtPlotItem::Legend, false);
        tube->setSamples(band);
        tube->attach(plot_);
        auto *line = new QwtPlotCurve(r.label);
        line->setPen(seriesColor(colour), r.reference ? 2.4 : 1.8);
        line->setRenderHint(QwtPlotItem::RenderAntialiased);
        line->setSamples(median);
        line->attach(plot_);
        ++colour;
    }
    table_->resizeColumnsToContents();
    plot_->setAxisAutoScale(QwtPlot::xBottom);
    plot_->setAxisAutoScale(QwtPlot::yLeft);
    plot_->replot();
}

} // namespace studio
