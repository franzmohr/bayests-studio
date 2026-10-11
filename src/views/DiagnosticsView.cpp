#include "views/DiagnosticsView.h"

#include "views/PlotStyle.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSplitter>
#include <QVBoxLayout>

#include <qwt_interval.h>
#include <qwt_plot.h>
#include <qwt_plot_curve.h>
#include <qwt_plot_histogram.h>
#include <qwt_samples.h>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace studio
{

namespace
{

double quantile(std::vector<double> sorted, double q)
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

} // namespace

DiagnosticsView::DiagnosticsView(QWidget *parent) : QWidget(parent)
{
    title_ = new QLabel(tr("Select a dataset under /posterior in the navigator."));
    title_->setObjectName(QStringLiteral("viewTitle"));

    row_ = new QComboBox;
    row_->setMinimumContentsLength(12);
    connect(row_, &QComboBox::currentIndexChanged, this, &DiagnosticsView::showRow);

    stats_ = new QLabel;
    stats_->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto *header = new QHBoxLayout;
    header->addWidget(title_, 1);
    header->addWidget(new QLabel(tr("Parameter")));
    header->addWidget(row_);

    trace_ = new QwtPlot;
    density_ = new QwtPlot;
    styleAxes(trace_, tr("Draw"), tr("Value"));
    styleAxes(density_, tr("Value"), tr("Share of draws"));
    trace_->setTitle(tr("Trace"));
    density_->setTitle(tr("Marginal posterior"));

    traceCurve_ = new QwtPlotCurve;
    traceCurve_->setPen(seriesColor(0), 1.0);
    traceCurve_->setRenderHint(QwtPlotItem::RenderAntialiased);
    traceCurve_->attach(trace_);

    meanCurve_ = new QwtPlotCurve;
    meanCurve_->setPen(seriesColor(1), 1.5, Qt::DashLine);
    meanCurve_->attach(trace_);

    histogram_ = new QwtPlotHistogram;
    histogram_->setStyle(QwtPlotHistogram::Columns);
    histogram_->setBrush(seriesColor(0, 160));
    histogram_->setPen(Qt::NoPen);
    histogram_->attach(density_);

    auto *plots = new QSplitter(Qt::Horizontal);
    plots->addWidget(trace_);
    plots->addWidget(density_);
    plots->setStretchFactor(0, 3);
    plots->setStretchFactor(1, 2);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(header);
    layout->addWidget(plots, 1);
    layout->addWidget(stats_);
}

void DiagnosticsView::clear()
{
    matrix_ = {};
    path_.clear();
    title_->setText(tr("Select a dataset under /posterior in the navigator."));
    row_->clear();
    stats_->clear();
    traceCurve_->setSamples(QVector<QPointF>{});
    meanCurve_->setSamples(QVector<QPointF>{});
    histogram_->setSamples(QVector<QwtIntervalSample>{});
    trace_->replot();
    density_->replot();
}

void DiagnosticsView::showDataset(const QString &file, const QString &path, const QStringList &rowNames)
{
    clear();
    try
    {
        matrix_ = readMatrix(file, path);
    }
    catch (const std::exception &e)
    {
        title_->setText(tr("%1 cannot be shown: %2").arg(path, QString::fromUtf8(e.what())));
        return;
    }
    path_ = path;
    title_->setText(tr("%1 — %2 parameters × %3 draws")
                        .arg(path).arg(matrix_.rows).arg(matrix_.cols));

    const QSignalBlocker block(row_);
    for (std::size_t r = 0; r < matrix_.rows; ++r)
    {
        row_->addItem(r < static_cast<std::size_t>(rowNames.size()) ? rowNames[static_cast<qsizetype>(r)]
                                                                     : tr("Row %1").arg(r + 1));
    }
    showRow(0);
}

void DiagnosticsView::showRow(int row)
{
    if (row < 0 || static_cast<std::size_t>(row) >= matrix_.rows || matrix_.cols == 0)
    {
        return;
    }
    const auto r = static_cast<std::size_t>(row);

    std::vector<double> draws(matrix_.cols);
    QVector<QPointF> trace(static_cast<qsizetype>(matrix_.cols));
    for (std::size_t c = 0; c < matrix_.cols; ++c)
    {
        draws[c] = matrix_.at(r, c);
        trace[static_cast<qsizetype>(c)] = QPointF(static_cast<double>(c + 1), draws[c]);
    }
    traceCurve_->setSamples(trace);

    const double n = static_cast<double>(draws.size());
    const double mean = std::accumulate(draws.begin(), draws.end(), 0.0) / n;
    double ss = 0.0;
    for (const double d : draws)
    {
        ss += (d - mean) * (d - mean);
    }
    const double sd = draws.size() > 1 ? std::sqrt(ss / (n - 1.0)) : 0.0;
    meanCurve_->setSamples(QVector<QPointF>{{1.0, mean}, {n, mean}});

    std::vector<double> sorted = draws;
    std::sort(sorted.begin(), sorted.end());
    const double lo = sorted.front();
    const double hi = sorted.back();

    // Freedman–Diaconis bin width, bounded to a readable number of bins.
    const double iqr = quantile(sorted, 0.75) - quantile(sorted, 0.25);
    int bins = 30;
    if (iqr > 0.0)
    {
        const double width = 2.0 * iqr / std::cbrt(n);
        bins = std::clamp(static_cast<int>(std::ceil((hi - lo) / width)), 10, 80);
    }
    QVector<QwtIntervalSample> hist;
    if (hi > lo)
    {
        const double width = (hi - lo) / bins;
        std::vector<double> counts(static_cast<std::size_t>(bins), 0.0);
        for (const double d : draws)
        {
            const int b = std::min(bins - 1, static_cast<int>((d - lo) / width));
            counts[static_cast<std::size_t>(b)] += 1.0 / n;
        }
        for (int b = 0; b < bins; ++b)
        {
            hist.append(QwtIntervalSample(counts[static_cast<std::size_t>(b)],
                                          QwtInterval(lo + b * width, lo + (b + 1) * width)));
        }
    }
    histogram_->setSamples(hist);

    stats_->setText(tr("Mean %1   SD %2   5% %3   Median %4   95% %5   Draws %6")
                        .arg(mean, 0, 'g', 5)
                        .arg(sd, 0, 'g', 5)
                        .arg(quantile(sorted, 0.05), 0, 'g', 5)
                        .arg(quantile(sorted, 0.5), 0, 'g', 5)
                        .arg(quantile(sorted, 0.95), 0, 'g', 5)
                        .arg(draws.size()));

    trace_->replot();
    density_->replot();
}

} // namespace studio
