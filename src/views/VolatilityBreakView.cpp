// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Franz X. Mohr

#include "views/VolatilityBreakView.h"

#include "views/FigureExport.h"
#include "views/PlotStyle.h"
#include "views/TableExport.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPen>
#include <QScrollArea>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <qwt_interval.h>
#include <qwt_legend.h>
#include <qwt_plot.h>
#include <qwt_plot_curve.h>
#include <qwt_plot_histogram.h>
#include <qwt_plot_intervalcurve.h>
#include <qwt_plot_marker.h>
#include <qwt_samples.h>
#include <qwt_text.h>

#include <cmath>

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

QLabel *section(const QString &text)
{
    auto *label = new QLabel(text);
    label->setObjectName(QStringLiteral("sectionTitle"));
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

QwtPlotMarker *vertical(QwtPlot *plot, double x, const QString &label)
{
    auto *m = new QwtPlotMarker(label);
    m->setLineStyle(QwtPlotMarker::VLine);
    m->setXValue(x);
    QColor c = plot->palette().color(QPalette::Text);
    c.setAlpha(110);
    m->setLinePen(c, 1.0, Qt::DashLine);
    QwtText t(label);
    QFont f = plot->axisFont(QwtPlot::xBottom);
    t.setFont(f);
    t.setColor(c);
    m->setLabel(t);
    m->setLabelAlignment(Qt::AlignLeft | Qt::AlignTop);
    m->setLabelOrientation(Qt::Vertical);
    m->attach(plot);
    return m;
}

/// The beta density with shapes a and b at x in (0, 1).
double betaDensity(double x, double a, double b)
{
    if (x <= 0.0 || x >= 1.0)
    {
        return 0.0;
    }
    const double logB = std::lgamma(a) + std::lgamma(b) - std::lgamma(a + b);
    return std::exp((a - 1.0) * std::log(x) + (b - 1.0) * std::log1p(-x) - logB);
}

QTableWidgetItem *number(double v, int decimals)
{
    auto *item = new QTableWidgetItem(std::isfinite(v) ? QString::number(v, 'f', decimals) : QStringLiteral("—"));
    item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return item;
}

} // namespace

VolatilityBreakView::VolatilityBreakView(QWidget *parent) : QWidget(parent)
{
    status_ = subtle();

    path_ = new QwtPlot;
    path_->setMinimumHeight(280);
    styleAxes(path_, tr("Year"), tr("Scale of the shocks' standard deviation, s_t"));
    path_->setToolTip(tr("s_t multiplies every shock's standard deviation: one before the break, a free scale in "
                         "each of the first periods from it, then a decay back towards one at the rate ρ. Past "
                         "the dashed line the forecast draws its shocks with these scales."));
    const FigureProvider pathFigure = [this]() {
        return singleFigure(path_, tr("The volatility break: the scale of the shocks"),
                            QStringLiteral("volatility-break-path"));
    };
    attachFigureExport(path_, pathFigure);

    rho_ = new QwtPlot;
    rho_->setMinimumHeight(280);
    styleAxes(rho_, QStringLiteral("ρ"), tr("Density"));
    rho_->setToolTip(tr("The posterior of the decay rate ρ, as a histogram of the draws, against its beta prior: "
                        "how much the data have said about how fast the volatility returns to normal."));
    const FigureProvider rhoFigure = [this]() {
        return singleFigure(rho_, tr("The decay rate ρ: posterior and prior"), QStringLiteral("volatility-break-rho"));
    };
    attachFigureExport(rho_, rhoFigure);
    auto *legend = new QwtLegend;
    rho_->insertLegend(legend, QwtPlot::TopLegend);

    table_ = new QTableWidget;
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionMode(QAbstractItemView::NoSelection);
    table_->setFocusPolicy(Qt::NoFocus);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    attachTableExport(table_);

    auto *pathHead = new QHBoxLayout;
    pathHead->addWidget(section(tr("The scale of the shocks")), 1);
    pathHead->addWidget(figureExportButton(pathFigure));
    auto *rhoHead = new QHBoxLayout;
    rhoHead->addWidget(section(tr("The decay rate ρ")), 1);
    rhoHead->addWidget(figureExportButton(rhoFigure));
    auto *tableHead = new QHBoxLayout;
    tableHead->addWidget(section(tr("The free scales and ρ")), 1);
    tableHead->addWidget(tableExportButton(table_));

    auto *host = new QWidget;
    auto *inner = new QVBoxLayout(host);
    inner->setContentsMargins(0, 0, 8, 0);
    inner->addLayout(pathHead);
    inner->addWidget(path_, 2);
    auto *lower = new QHBoxLayout;
    auto *left = new QVBoxLayout;
    left->addLayout(rhoHead);
    left->addWidget(rho_, 1);
    auto *right = new QVBoxLayout;
    right->addLayout(tableHead);
    right->addWidget(table_);
    right->addStretch(1);
    lower->addLayout(left, 1);
    lower->addLayout(right, 1);
    inner->addLayout(lower, 1);
    auto *scroll = new QScrollArea;
    scroll->setWidget(host);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 8, 0, 0);
    layout->addWidget(status_);
    layout->addWidget(scroll, 1);

    watcher_ = new QFutureWatcher<Result>(this);
    connect(watcher_, &QFutureWatcher<Result>::finished, this, [this]() {
        if (pending_)
        {
            pending_ = false;
            setModel(file_);
            return;
        }
        show(watcher_->result());
    });
    setModel(QString());
}

void VolatilityBreakView::clear(const QString &message)
{
    status_->setText(message);
    for (QwtPlot *p : {path_, rho_})
    {
        p->detachItems(QwtPlotItem::Rtti_PlotCurve, true);
        p->detachItems(QwtPlotItem::Rtti_PlotIntervalCurve, true);
        p->detachItems(QwtPlotItem::Rtti_PlotHistogram, true);
        p->detachItems(QwtPlotItem::Rtti_PlotMarker, true);
        p->replot();
    }
    table_->clear();
    table_->setRowCount(0);
    table_->setColumnCount(0);
}

void VolatilityBreakView::setModel(const QString &file)
{
    file_ = file;
    if (file.isEmpty())
    {
        clear(tr("Select a model estimated with a volatility break (Model set-up › New Model…, under the inverse "
                 "Wishart prior)."));
        return;
    }
    if (watcher_->isRunning())
    {
        pending_ = true;
        return;
    }
    status_->setText(tr("Reading the draws…"));
    watcher_->setFuture(QtConcurrent::run([file]() {
        Result r;
        try
        {
            r.summary = std::make_shared<VolatilityBreakSummary>(volatilityBreakSummary(file));
        }
        catch (const std::exception &e)
        {
            r.error = QString::fromUtf8(e.what());
        }
        return r;
    }));
}

void VolatilityBreakView::show(const Result &result)
{
    if (!result.summary)
    {
        clear(result.error);
        return;
    }
    clear(QString());
    const VolatilityBreakSummary &s = *result.summary;
    status_->setText(tr("From %1 every shock's standard deviation is scaled by s_t: %2 free scales, then a decay "
                        "at the rate ρ (Lenza and Primiceri 2022). %3 draws; the bands are 68% and 90%.")
                         .arg(s.labelOf(s.breakColumn))
                         .arg(s.scales)
                         .arg(s.draws));

    // The path.
    QVector<QwtIntervalSample> b90, b68;
    QVector<QPointF> med;
    for (std::size_t t = 0; t < s.time.size(); ++t)
    {
        const auto tu = static_cast<arma::uword>(t);
        b90.append({s.time[t], QwtInterval(s.path(tu, 0), s.path(tu, 4))});
        b68.append({s.time[t], QwtInterval(s.path(tu, 1), s.path(tu, 3))});
        med.append({s.time[t], s.path(tu, 2)});
    }
    auto *t90 = tube(tr("90%"), seriesColor(0, 50));
    t90->setSamples(b90);
    t90->attach(path_);
    auto *t68 = tube(tr("68%"), seriesColor(0, 95));
    t68->setSamples(b68);
    t68->attach(path_);
    auto *line = new QwtPlotCurve(tr("Median"));
    line->setPen(seriesColor(0), 2.0);
    line->setRenderHint(QwtPlotItem::RenderAntialiased);
    line->setSamples(med);
    line->attach(path_);
    auto *one = new QwtPlotMarker;
    one->setLineStyle(QwtPlotMarker::HLine);
    one->setYValue(1.0);
    QColor c = path_->palette().color(QPalette::Text);
    c.setAlpha(110);
    one->setLinePen(c, 1.0);
    one->attach(path_);
    if (s.horizon > 0)
    {
        vertical(path_, s.sampleEnd, tr("forecast"));
    }
    path_->setAxisTitle(QwtPlot::xBottom, s.dated ? tr("Year") : tr("Period"));
    if (!s.time.empty())
    {
        path_->setAxisScale(QwtPlot::xBottom, s.time.front(), s.time.back());
    }
    path_->setAxisAutoScale(QwtPlot::yLeft);
    path_->replot();

    // rho: a histogram of the draws against the beta prior.
    const arma::rowvec rho = s.parameters.row(static_cast<arma::uword>(s.scales));
    const int bins = 40;
    arma::uvec counts = arma::hist(arma::vec(rho.t()), arma::linspace(0.0125, 0.9875, bins));
    QVector<QwtIntervalSample> bars;
    double peak = 0.0;
    for (int b = 0; b < bins; ++b)
    {
        const double density = static_cast<double>(counts(static_cast<arma::uword>(b))) / (rho.n_elem / double(bins));
        bars.append(QwtIntervalSample(density, b / double(bins), (b + 1) / double(bins)));
        peak = std::max(peak, density);
    }
    auto *hist = new QwtPlotHistogram(tr("Posterior"));
    hist->setStyle(QwtPlotHistogram::Columns);
    hist->setBrush(seriesColor(0, 140));
    hist->setPen(QPen(Qt::NoPen));
    hist->setSamples(bars);
    hist->attach(rho_);
    if (s.rhoShape1 > 0.0 && s.rhoShape2 > 0.0)
    {
        QVector<QPointF> prior;
        for (int i = 1; i < 200; ++i)
        {
            const double x = i / 200.0;
            const double y = betaDensity(x, s.rhoShape1, s.rhoShape2);
            prior.append({x, y});
            peak = std::max(peak, y);
        }
        auto *curve = new QwtPlotCurve(tr("Prior, beta(%1, %2)").arg(s.rhoShape1).arg(s.rhoShape2));
        curve->setPen(seriesColor(1), 2.0);
        curve->setRenderHint(QwtPlotItem::RenderAntialiased);
        curve->setSamples(prior);
        curve->attach(rho_);
    }
    rho_->setAxisScale(QwtPlot::xBottom, 0.0, 1.0);
    rho_->setAxisScale(QwtPlot::yLeft, 0.0, peak * 1.08);
    rho_->replot();

    // The table.
    const QStringList columns = {tr("Mean"), tr("SD"), tr("5%"), tr("16%"), tr("Median"), tr("84%"), tr("95%")};
    table_->setRowCount(static_cast<int>(s.table.n_rows));
    table_->setColumnCount(static_cast<int>(columns.size()) + 1);
    table_->setHorizontalHeaderLabels(QStringList(columns) << tr("Prior"));
    table_->setVerticalHeaderLabels(s.labels);
    for (arma::uword r = 0; r < s.table.n_rows; ++r)
    {
        const bool isRho = r == static_cast<arma::uword>(s.scales);
        for (arma::uword j = 0; j < s.table.n_cols; ++j)
        {
            table_->setItem(static_cast<int>(r), static_cast<int>(j), number(s.table(r, j), isRho ? 3 : 2));
        }
        auto *prior = new QTableWidgetItem(
            isRho ? tr("beta(%1, %2)").arg(s.rhoShape1).arg(s.rhoShape2) : tr("Pareto(1, %1)").arg(s.shape));
        prior->setTextAlignment(Qt::AlignCenter);
        table_->setItem(static_cast<int>(r), static_cast<int>(s.table.n_cols), prior);
        if (!isRho && static_cast<int>(r) + s.breakColumn > s.periods)
        {
            // A free scale past the sample: the data say nothing about it.
            for (int j = 0; j < table_->columnCount(); ++j)
            {
                table_->item(static_cast<int>(r), j)
                    ->setToolTip(tr("This period lies after the sample: its scale is drawn from the prior."));
            }
        }
    }
    // The prior's name in full; the numbers share the rest.
    table_->horizontalHeader()->setSectionResizeMode(table_->columnCount() - 1, QHeaderView::ResizeToContents);
    table_->setFixedHeight(table_->horizontalHeader()->sizeHint().height() +
                           table_->verticalHeader()->defaultSectionSize() * table_->rowCount() +
                           2 * table_->frameWidth() + 2);
    setTableExportInfo(table_, tr("The volatility break: posterior of the free scales and ρ"),
                       tr("Lenza and Primiceri (2022): from %1 the shocks' standard deviations are scaled by s_t, "
                          "%2 free scales and then a decay at the rate ρ; %3 draws.")
                           .arg(s.labelOf(s.breakColumn))
                           .arg(s.scales)
                           .arg(s.draws));
}

} // namespace studio
