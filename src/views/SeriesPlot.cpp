#include "views/SeriesPlot.h"

#include "views/PlotStyle.h"

#include <QPen>
#include <QVBoxLayout>

#include <qwt_interval.h>
#include <qwt_legend.h>
#include <qwt_plot.h>
#include <qwt_plot_curve.h>
#include <qwt_plot_intervalcurve.h>
#include <qwt_samples.h>

#include <algorithm>
#include <cmath>

namespace studio
{

double sortedQuantile(const std::vector<double> &sorted, double q)
{
    if (sorted.empty())
    {
        return std::nan("");
    }
    const double pos = q * static_cast<double>(sorted.size() - 1);
    const auto lo = static_cast<std::size_t>(std::floor(pos));
    const auto hi = std::min(lo + 1, sorted.size() - 1);
    return sorted[lo] + (pos - static_cast<double>(lo)) * (sorted[hi] - sorted[lo]);
}

SeriesPlot::SeriesPlot(const QString &xTitle, const QString &yTitle, QWidget *parent) : QWidget(parent)
{
    plot_ = new QwtPlot;
    styleAxes(plot_, xTitle, yTitle);
    plot_->insertLegend(new QwtLegend, QwtPlot::BottomLegend);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(plot_);
}

void SeriesPlot::clear()
{
    plot_->detachItems(QwtPlotItem::Rtti_PlotCurve, true);
    plot_->detachItems(QwtPlotItem::Rtti_PlotIntervalCurve, true);
    plot_->replot();
}

void SeriesPlot::addLine(const QString &name, const std::vector<double> &y, int colorIndex, double xOffset)
{
    QVector<QPointF> points;
    points.reserve(static_cast<qsizetype>(y.size()));
    for (std::size_t t = 0; t < y.size(); ++t)
    {
        points.append({xOffset + static_cast<double>(t + 1), y[t]});
    }
    auto *curve = new QwtPlotCurve(name);
    curve->setPen(seriesColor(colorIndex), 1.6);
    curve->setRenderHint(QwtPlotItem::RenderAntialiased);
    curve->setSamples(points);
    curve->attach(plot_);
}

void SeriesPlot::addFan(const QString &name, const std::vector<std::vector<double>> &draws, int colorIndex,
                        double xOffset)
{
    QVector<QwtIntervalSample> b90;
    QVector<QwtIntervalSample> b68;
    QVector<QPointF> med;
    for (std::size_t t = 0; t < draws.size(); ++t)
    {
        std::vector<double> sorted = draws[t];
        std::sort(sorted.begin(), sorted.end());
        const double x = xOffset + static_cast<double>(t + 1);
        b90.append({x, QwtInterval(sortedQuantile(sorted, 0.05), sortedQuantile(sorted, 0.95))});
        b68.append({x, QwtInterval(sortedQuantile(sorted, 0.16), sortedQuantile(sorted, 0.84))});
        med.append({x, sortedQuantile(sorted, 0.5)});
    }
    for (const auto &[samples, alpha, label] :
         {std::tuple{b90, 50, tr("90% band")}, std::tuple{b68, 95, tr("68% band")}})
    {
        auto *band = new QwtPlotIntervalCurve(label);
        band->setStyle(QwtPlotIntervalCurve::Tube);
        band->setPen(QPen(Qt::transparent, 0.0));
        band->setBrush(seriesColor(colorIndex, alpha));
        band->setRenderHint(QwtPlotItem::RenderAntialiased);
        band->setSamples(samples);
        band->attach(plot_);
    }
    auto *median = new QwtPlotCurve(name);
    median->setPen(seriesColor(colorIndex), 2.0, Qt::DashLine);
    median->setRenderHint(QwtPlotItem::RenderAntialiased);
    median->setSamples(med);
    median->attach(plot_);
}

void SeriesPlot::replot()
{
    plot_->setAxisAutoScale(QwtPlot::xBottom);
    plot_->setAxisAutoScale(QwtPlot::yLeft);
    plot_->replot();
}

} // namespace studio
