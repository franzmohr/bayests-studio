// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Franz X. Mohr

#include "views/MultipliersView.h"

#include "engine/ModelFileTree.h"
#include "views/PlotStyle.h"
#include "views/SeriesPlot.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPen>
#include <QScrollArea>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <qwt_interval.h>
#include <qwt_plot.h>
#include <qwt_plot_curve.h>
#include <qwt_plot_intervalcurve.h>
#include <qwt_plot_marker.h>
#include <qwt_samples.h>
#include <qwt_text.h>

#include <algorithm>

namespace studio
{

namespace
{

QLabel *subtle(const QString &text = {})
{
    auto *label = new QLabel(text);
    label->setObjectName(QStringLiteral("subtle"));
    label->setWordWrap(true);
    return label;
}

QwtPlotIntervalCurve *tube(const QString &title, const QColor &color)
{
    auto *curve = new QwtPlotIntervalCurve(title);
    curve->setStyle(QwtPlotIntervalCurve::Tube);
    curve->setPen(QPen(Qt::transparent, 0.0));
    curve->setBrush(color);
    curve->setRenderHint(QwtPlotItem::RenderAntialiased);
    return curve;
}

} // namespace

MultipliersView::MultipliersView(QWidget *parent) : QWidget(parent)
{
    status_ = subtle();
    impulse_ = new QComboBox;
    impulse_->setMinimumContentsLength(10);
    impulse_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    impulse_->setToolTip(tr("The exogenous variable whose change the multipliers trace."));
    type_ = new QComboBox;
    type_->addItem(tr("Permanent: held from period 0"), true);
    type_->addItem(tr("Transitory: period 0 alone"), false);
    type_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    type_->setToolTip(tr("Permanent: the variable is raised in period 0 and stays raised. Transitory: it is raised in period 0 only."));
    horizon_ = new QSpinBox;
    horizon_->setRange(0, 200);
    horizon_->setValue(20);
    horizon_->setToolTip(tr("How many periods after the change the multipliers run."));
    shock_ = new QDoubleSpinBox;
    shock_->setRange(-1e6, 1e6);
    shock_->setDecimals(3);
    shock_->setValue(1.0);
    shock_->setToolTip(tr("The size of the change, in the exogenous variable's own units."));
    cumulative_ = new QCheckBox(tr("Cumulative"));
    cumulative_->setToolTip(tr("The multipliers summed over the horizons."));
    period_ = new QSpinBox;
    period_->setMinimum(1);
    period_->setToolTip(tr("For a model whose coefficients or volatilities move: the period of the sample whose system the analysis describes. The last period by default."));
    periodLabel_ = new QLabel(tr("Period"));

    auto *controls = new QHBoxLayout;
    controls->addWidget(new QLabel(tr("Change in")));
    controls->addWidget(impulse_);
    controls->addSpacing(12);
    controls->addWidget(type_);
    controls->addSpacing(12);
    controls->addWidget(new QLabel(tr("by")));
    controls->addWidget(shock_);
    controls->addSpacing(12);
    controls->addWidget(new QLabel(tr("Horizon")));
    controls->addWidget(horizon_);
    controls->addSpacing(12);
    controls->addWidget(periodLabel_);
    controls->addWidget(period_);
    controls->addSpacing(12);
    controls->addWidget(cumulative_);
    controls->addStretch(1);
    figure_ = [this]() {
        FigureSpec spec;
        const MultiplierRequest req = request();
        spec.subtitle = tr("Median, 68% and 90% bands%1").arg(req.cumulative ? tr(", cumulative") : QString());
        spec.columns = columns_;
        for (QwtPlot *p : plots_)
        {
            spec.cells.push_back({p, {}});
        }
        if (matrix_)
        {
            // As on screen: a column per exogenous variable, a row per response.
            spec.title = tr("Dynamic multipliers of a %1 change of %2")
                             .arg(req.permanent ? tr("permanent") : tr("transitory"))
                             .arg(req.shock);
            for (const QString &x : support_.exogenous)
            {
                spec.columnLabels << tr("Change in %1").arg(x);
            }
            spec.rowLabels = support_.variables;
            spec.name = QStringLiteral("multipliers-all");
            return spec;
        }
        spec.title = tr("Dynamic multipliers of a %1 change in %2")
                         .arg(req.permanent ? tr("permanent") : tr("transitory"), impulse_->currentText());
        spec.name = QStringLiteral("multipliers-%1").arg(impulse_->currentText());
        return spec;
    };
    controls->addWidget(figureExportButton(figure_));

    gridHost_ = new QWidget;
    grid_ = new QGridLayout(gridHost_);
    grid_->setSpacing(12);
    auto *scroll = new QScrollArea;
    scroll->setWidget(gridHost_);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 8, 0, 0);
    layout->addLayout(controls);
    layout->addWidget(status_);
    layout->addWidget(scroll, 1);

    watcher_ = new QFutureWatcher<Result>(this);
    connect(watcher_, &QFutureWatcher<Result>::finished, this, [this]() {
        show(watcher_->result());
        if (pending_)
        {
            pending_ = false;
            compute();
        }
    });
    debounce_ = new QTimer(this);
    debounce_->setSingleShot(true);
    debounce_->setInterval(200);
    connect(debounce_, &QTimer::timeout, this, &MultipliersView::compute);
    for (QComboBox *c : {impulse_, type_})
    {
        connect(c, &QComboBox::currentIndexChanged, this, &MultipliersView::schedule);
    }
    for (QSpinBox *s : {horizon_, period_})
    {
        connect(s, &QSpinBox::valueChanged, this, &MultipliersView::schedule);
    }
    connect(shock_, &QDoubleSpinBox::valueChanged, this, &MultipliersView::schedule);
    connect(cumulative_, &QCheckBox::toggled, this, &MultipliersView::schedule);
    // All exogenous variables and one need grids of their own.
    connect(impulse_, &QComboBox::currentIndexChanged, this, [this]() {
        if (support_.ok && allImpulses() != matrix_)
        {
            rebuildGrid();
        }
    });
    setModel(QString());
}

void MultipliersView::setModel(const QString &file)
{
    file_ = file;
    support_ = file.isEmpty() ? MultiplierSupport{} : multiplierSupport(file);
    const bool ok = support_.ok;
    for (QWidget *w : std::initializer_list<QWidget *>{impulse_, type_, horizon_, shock_, cumulative_, period_})
    {
        w->setEnabled(ok);
    }
    if (!ok)
    {
        status_->setText(file.isEmpty() ? tr("Select a model of the project.")
                                        : tr("Not available for this model: %1").arg(support_.reason));
        rebuildGrid();
        return;
    }
    {
        const QSignalBlocker b1(impulse_);
        const QSignalBlocker b2(period_);
        impulse_->clear();
        impulse_->addItem(tr("All exogenous variables"));
        impulse_->addItems(support_.exogenous);
        // The matrix while it stays legible; with one exogenous variable it
        // would be a single column, which the grid shows better.
        impulse_->setCurrentIndex(support_.m > 1 && support_.k <= 6 && support_.m <= 6 ? 0 : 1);
        period_->setMaximum(std::max(1, support_.periods));
        period_->setValue(std::max(1, support_.periods));
    }
    period_->setVisible(support_.moving);
    periodLabel_->setVisible(support_.moving);
    rebuildGrid();
    schedule();
}

MultiplierRequest MultipliersView::request() const
{
    MultiplierRequest r;
    r.impulse = std::max(0, impulse_->currentIndex() - 1);
    r.horizon = horizon_->value();
    r.shock = shock_->value();
    r.permanent = type_->currentData().toBool();
    r.cumulative = cumulative_->isChecked();
    r.period = support_.moving ? period_->value() : -1;
    return r;
}

void MultipliersView::schedule()
{
    if (support_.ok)
    {
        debounce_->start();
    }
}

void MultipliersView::compute()
{
    if (!support_.ok)
    {
        return;
    }
    if (watcher_->isRunning())
    {
        pending_ = true;
        return;
    }
    status_->setText(tr("Computing…"));
    const QString file = file_;
    const MultiplierRequest req = request();
    std::vector<int> impulses;
    if (allImpulses())
    {
        for (int j = 0; j < support_.m; ++j)
        {
            impulses.push_back(j);
        }
    }
    else
    {
        impulses.push_back(req.impulse);
    }
    watcher_->setFuture(QtConcurrent::run([file, req, impulses]() {
        const std::lock_guard<std::recursive_mutex> hdf5(hdf5Mutex());
        Result r;
        try
        {
            r.impulses = impulses;
            for (const int j : impulses)
            {
                MultiplierRequest one = req;
                one.impulse = j;
                r.draws.push_back(dynamicMultipliers(file, one));
            }
        }
        catch (const std::exception &e)
        {
            r.error = QString::fromUtf8(e.what());
        }
        return r;
    }));
}

bool MultipliersView::allImpulses() const
{
    return impulse_->currentIndex() == 0;
}

void MultipliersView::rebuildGrid()
{
    // Out of the grid at once, so no old panel stands beside the new ones.
    for (QWidget *w : std::vector<QWidget *>(plots_.begin(), plots_.end()))
    {
        grid_->removeWidget(w);
        w->hide();
        w->deleteLater();
    }
    plots_.clear();
    for (QWidget *h : headers_)
    {
        grid_->removeWidget(h);
        h->hide();
        h->deleteLater();
    }
    headers_.clear();
    const int k = support_.ok ? support_.k : 0;
    const int m = support_.ok ? support_.m : 0;
    matrix_ = k > 0 && m > 0 && allImpulses();
    QFont bold = font();
    bold.setBold(true);
    if (matrix_)
    {
        // As the impulse responses: exogenous variable j's column, response
        // i's row, the headers along the top and the left.
        columns_ = m;
        for (int j = 0; j < m; ++j)
        {
            auto *head = new QLabel(tr("Change in %1").arg(support_.exogenous.value(j)));
            head->setAlignment(Qt::AlignCenter);
            head->setFont(bold);
            head->setWordWrap(true);
            grid_->addWidget(head, 0, j + 1);
            headers_.push_back(head);
        }
        for (int i = 0; i < k; ++i)
        {
            auto *row = new QLabel(support_.variables.value(i));
            row->setAlignment(Qt::AlignCenter);
            row->setFont(bold);
            row->setMinimumWidth(70);
            row->setToolTip(tr("The multipliers of %1, for a change in each exogenous variable along the row.")
                                .arg(support_.variables.value(i)));
            grid_->addWidget(row, i + 1, 0);
            headers_.push_back(row);
            for (int j = 0; j < m; ++j)
            {
                auto *plot = new QwtPlot;
                attachFigureExport(plot, figure_);
                plot->setToolTip(tr("The response of %1 to a change in %2, in its own units: the posterior median with 68% and 90% bands. Right-click to copy or save the figure.")
                                     .arg(support_.variables.value(i), support_.exogenous.value(j)));
                plot->setMinimumHeight(170);
                styleAxes(plot, i + 1 == k ? tr("Horizon") : QString(), QString());
                grid_->addWidget(plot, i + 1, j + 1);
                plots_.push_back(plot);
            }
        }
        return;
    }
    const int columns = k <= 4 ? std::max(1, std::min(k, 2)) : 3;
    columns_ = columns;
    for (int i = 0; i < k; ++i)
    {
        auto *plot = new QwtPlot;
        attachFigureExport(plot, figure_);
        plot->setToolTip(tr("The multiplier for this variable: the posterior median with 68% and 90% bands. Right-click to copy or save the figure."));
        plot->setMinimumHeight(220);
        styleAxes(plot, tr("Horizon"), QString());
        QwtText title(support_.variables.value(i));
        title.setFont(bold);
        plot->setTitle(title);
        grid_->addWidget(plot, i / columns, i % columns);
        plots_.push_back(plot);
    }
}

void MultipliersView::show(const Result &result)
{
    if (!result.error.isEmpty())
    {
        status_->setText(tr("✗  %1").arg(result.error));
        return;
    }
    const MultiplierRequest req = request();
    // A result computed for the other layout is stale; the next one is coming.
    if (result.draws.empty() || (result.impulses.size() > 1) != (matrix_ && support_.m > 1))
    {
        return;
    }
    const int draws = static_cast<int>(result.draws.front().size());
    const QString period = support_.moving ? tr(", at period %1").arg(period_->value()) : QString();
    status_->setText(matrix_ ? tr("%1 change of %2 in each exogenous variable (columns), in its own units: the "
                                  "response of each variable (rows); median, 68% and 90% bands over %3 draws%4.")
                                   .arg(req.permanent ? tr("A permanent") : tr("A transitory"))
                                   .arg(req.shock)
                                   .arg(draws)
                                   .arg(period)
                             : tr("%1 change of %2 in %3: the response of each variable, in its own units; median, "
                                  "68% and 90% bands over %4 draws%5.")
                                   .arg(req.permanent ? tr("A permanent") : tr("A transitory"))
                                   .arg(req.shock)
                                   .arg(impulse_->currentText())
                                   .arg(draws)
                                   .arg(period));
    const int horizons = req.horizon + 1;
    std::vector<double> column(static_cast<std::size_t>(draws));
    const auto m = static_cast<std::size_t>(std::max(1, support_.m));
    for (std::size_t p = 0; p < plots_.size(); ++p)
    {
        // Plot p is the response of v to a change in exogenous variable s.
        const std::size_t v = matrix_ ? p / m : p;
        const std::size_t s = matrix_ ? p % m : 0;
        if (s >= result.draws.size())
        {
            continue;
        }
        const std::vector<arma::mat> &change = result.draws[s];
        QwtPlot *plot = plots_[p];
        plot->detachItems(QwtPlotItem::Rtti_PlotCurve, true);
        plot->detachItems(QwtPlotItem::Rtti_PlotIntervalCurve, true);
        plot->detachItems(QwtPlotItem::Rtti_PlotMarker, true);
        auto *zero = new QwtPlotMarker;
        zero->setLineStyle(QwtPlotMarker::HLine);
        zero->setYValue(0.0);
        QColor c = plot->palette().color(QPalette::Text);
        c.setAlpha(110);
        zero->setLinePen(c, 1.0);
        zero->attach(plot);
        QVector<QwtIntervalSample> b90, b68;
        QVector<QPointF> med;
        for (int h = 0; h < horizons && draws > 0; ++h)
        {
            for (int d = 0; d < draws; ++d)
            {
                column[static_cast<std::size_t>(d)] =
                    change[static_cast<std::size_t>(d)](static_cast<arma::uword>(h), static_cast<arma::uword>(v));
            }
            std::sort(column.begin(), column.end());
            b90.append({double(h), QwtInterval(sortedQuantile(column, 0.05), sortedQuantile(column, 0.95))});
            b68.append({double(h), QwtInterval(sortedQuantile(column, 0.16), sortedQuantile(column, 0.84))});
            med.append({double(h), sortedQuantile(column, 0.5)});
        }
        auto *t90 = tube(tr("90%"), seriesColor(0, 50));
        t90->setSamples(b90);
        t90->attach(plot);
        auto *t68 = tube(tr("68%"), seriesColor(0, 95));
        t68->setSamples(b68);
        t68->attach(plot);
        auto *line = new QwtPlotCurve(tr("Median"));
        line->setPen(seriesColor(0), 2.0);
        line->setRenderHint(QwtPlotItem::RenderAntialiased);
        line->setSamples(med);
        line->attach(plot);
        plot->setAxisScale(QwtPlot::xBottom, 0.0, std::max(1.0, double(req.horizon)));
        plot->replot();
    }
}

} // namespace studio
