// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Franz X. Mohr

#include "views/BandPlot.h"

#include "views/PlotStyle.h"

#include <QObject>
#include <QPen>

#include <qwt_interval.h>
#include <qwt_plot.h>
#include <qwt_plot_curve.h>
#include <qwt_plot_intervalcurve.h>
#include <qwt_plot_marker.h>
#include <qwt_samples.h>

namespace studio
{

namespace
{

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

void clearBandPlot(QwtPlot *plot)
{
    plot->detachItems(QwtPlotItem::Rtti_PlotCurve, true);
    plot->detachItems(QwtPlotItem::Rtti_PlotIntervalCurve, true);
    plot->detachItems(QwtPlotItem::Rtti_PlotMarker, true);
}

void drawPathBands(QwtPlot *plot, const std::vector<double> &x, const PathBands &bands, int color, bool zero)
{
    if (zero)
    {
        auto *line = new QwtPlotMarker;
        line->setLineStyle(QwtPlotMarker::HLine);
        line->setYValue(0.0);
        QColor c = plot->palette().color(QPalette::Text);
        c.setAlpha(110);
        line->setLinePen(c, 1.0);
        line->attach(plot);
    }
    QVector<QwtIntervalSample> b90, b68;
    QVector<QPointF> median;
    const std::size_t n = std::min(x.size(), bands.median.size());
    for (std::size_t t = 0; t < n; ++t)
    {
        b90.append({x[t], QwtInterval(bands.lo90[t], bands.hi90[t])});
        b68.append({x[t], QwtInterval(bands.lo68[t], bands.hi68[t])});
        median.append({x[t], bands.median[t]});
    }
    auto *t90 = tube(QObject::tr("90%"), seriesColor(color, 50));
    t90->setSamples(b90);
    t90->attach(plot);
    auto *t68 = tube(QObject::tr("68%"), seriesColor(color, 95));
    t68->setSamples(b68);
    t68->attach(plot);
    auto *line = new QwtPlotCurve(QObject::tr("Median"));
    line->setPen(seriesColor(color), 1.8);
    line->setRenderHint(QwtPlotItem::RenderAntialiased);
    line->setSamples(median);
    line->attach(plot);
}

void drawPathLine(QwtPlot *plot, const std::vector<double> &x, const std::vector<double> &y, int color,
                  const QString &name)
{
    QVector<QPointF> points;
    for (std::size_t t = 0; t < std::min(x.size(), y.size()); ++t)
    {
        points.append({x[t], y[t]});
    }
    auto *line = new QwtPlotCurve(name);
    line->setPen(seriesColor(color), 1.2);
    line->setRenderHint(QwtPlotItem::RenderAntialiased);
    line->setSamples(points);
    line->attach(plot);
}

} // namespace studio
