#pragma once

#include <QWidget>

#include <vector>

class QwtPlot;
class QwtPlotCurve;
class QwtPlotIntervalCurve;
class QwtLegend;

namespace studio
{

/// A time series plot with optional fan: lines for observed series, and for a
/// forecast a median with 68% and 90% bands. Used by the Data and Forecasting
/// pages so both look alike.
class SeriesPlot : public QWidget
{
    Q_OBJECT

public:
    explicit SeriesPlot(const QString &xTitle, const QString &yTitle, QWidget *parent = nullptr);

    void clear();

    /// One line per series; x runs 1..n.
    void addLine(const QString &name, const std::vector<double> &y, int colorIndex, double xOffset = 0.0);

    /// A fan from per-period draws: draws[t] holds all draws of period t,
    /// plotted at x = xOffset + t + 1.
    void addFan(const QString &name, const std::vector<std::vector<double>> &draws, int colorIndex,
                double xOffset);

    void replot();

    /// The title an exported figure carries.
    void setExportTitle(const QString &title) { title_ = title; }

private:
    QwtPlot *plot_;
    QString title_;
};

/// Quantile of a sorted sample, linear between order statistics.
double sortedQuantile(const std::vector<double> &sorted, double q);

} // namespace studio
