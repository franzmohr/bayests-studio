// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Franz X. Mohr

#include "views/DiagnosticsView.h"

#include "views/FigureExport.h"
#include "views/PlotStyle.h"

#include <QComboBox>
#include <QEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPen>
#include <QScrollArea>
#include <QSplitter>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <qwt_interval.h>
#include <qwt_plot.h>
#include <qwt_plot_canvas.h>
#include <qwt_plot_curve.h>
#include <qwt_plot_histogram.h>
#include <qwt_plot_marker.h>
#include <qwt_samples.h>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace studio
{

namespace
{

/// More cells than this and the block is not drawn: a plot each would take
/// longer to build than to read, and none would be legible.
constexpr int maxCells = 600;

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

/// A histogram as shares of the draws, the Freedman–Diaconis bin width
/// bounded to between `minBins` and `maxBins` bins.
QVector<QwtIntervalSample> histogramOf(const std::vector<double> &sorted, int minBins, int maxBins)
{
    QVector<QwtIntervalSample> hist;
    if (sorted.empty())
    {
        return hist;
    }
    const double n = static_cast<double>(sorted.size());
    const double lo = sorted.front();
    const double hi = sorted.back();
    const double iqr = quantile(sorted, 0.75) - quantile(sorted, 0.25);
    int bins = (minBins + maxBins) / 2;
    if (iqr > 0.0)
    {
        const double width = 2.0 * iqr / std::cbrt(n);
        bins = std::clamp(static_cast<int>(std::ceil((hi - lo) / width)), minBins, maxBins);
    }
    if (!(hi > lo))
    {
        return hist;
    }
    const double width = (hi - lo) / bins;
    std::vector<double> counts(static_cast<std::size_t>(bins), 0.0);
    for (const double d : sorted)
    {
        const int b = std::min(bins - 1, static_cast<int>((d - lo) / width));
        counts[static_cast<std::size_t>(b)] += 1.0 / n;
    }
    for (int b = 0; b < bins; ++b)
    {
        hist.append(QwtIntervalSample(counts[static_cast<std::size_t>(b)],
                                      QwtInterval(lo + b * width, lo + (b + 1) * width)));
    }
    return hist;
}

std::vector<double> rowOf(const Matrix &m, std::size_t r)
{
    std::vector<double> draws(m.cols);
    for (std::size_t c = 0; c < m.cols; ++c)
    {
        draws[c] = m.at(r, c);
    }
    return draws;
}

QLabel *header(const QString &text, Qt::Alignment align)
{
    auto *label = new QLabel(text);
    label->setObjectName(QStringLiteral("subtle"));
    label->setAlignment(align);
    label->setWordWrap(true);
    label->setToolTip(text);
    return label;
}

} // namespace

BlockLayout blockLayout(std::size_t rows, const QStringList &rowNames, const QStringList &variables)
{
    BlockLayout out;
    const auto n = static_cast<qsizetype>(rows);
    const auto place = [&](const std::vector<std::pair<QString, QString>> &pairs) {
        for (const auto &[r, c] : pairs)
        {
            if (!out.rowLabels.contains(r))
            {
                out.rowLabels << r;
            }
            if (!out.columnLabels.contains(c))
            {
                out.columnLabels << c;
            }
        }
        out.cell.assign(static_cast<std::size_t>(out.rows() * out.columns()), -1);
        for (std::size_t i = 0; i < pairs.size(); ++i)
        {
            const auto r = out.rowLabels.indexOf(pairs[i].first);
            const auto c = out.columnLabels.indexOf(pairs[i].second);
            int &slot = out.cell[static_cast<std::size_t>(r * out.columns() + c)];
            if (slot >= 0)
            {
                out = {};   // two parameters in one cell: not a matrix after all
                return;
            }
            slot = static_cast<int>(i);
        }
    };

    if (n > 0 && rowNames.size() == n)
    {
        // "equation ← regressor", the builder's names of a/coeffs.
        const QString arrow = QStringLiteral(" ← ");
        std::vector<std::pair<QString, QString>> pairs;
        bool named = true;
        for (const QString &name : rowNames)
        {
            const auto at = name.indexOf(arrow);
            if (at < 0)
            {
                named = false;
                break;
            }
            pairs.emplace_back(name.left(at), name.mid(at + arrow.size()));
        }
        if (!named)
        {
            // "ec1: term", beta's: a term per row, a relation per column.
            pairs.clear();
            named = true;
            for (const QString &name : rowNames)
            {
                const auto at = name.indexOf(QStringLiteral(": "));
                if (at < 0)
                {
                    named = false;
                    break;
                }
                pairs.emplace_back(name.mid(at + 2), name.left(at));
            }
        }
        if (named)
        {
            place(pairs);
            return out;
        }
    }
    // Unnamed: k x k of the variables, vec() column by column -- a
    // covariance or precision is symmetric, so the order is the same both ways.
    const auto k = variables.size();
    if (k > 1 && n == k)
    {
        // k of them: a diagonal, a stochastic volatility model's precisions.
        out.rowLabels = variables;
        out.columnLabels = variables;
        out.cell.assign(static_cast<std::size_t>(k * k), -1);
        for (qsizetype i = 0; i < k; ++i)
        {
            out.cell[static_cast<std::size_t>(i * k + i)] = static_cast<int>(i);
        }
        return out;
    }
    if (k > 0 && n == k * k)
    {
        out.rowLabels = variables;
        out.columnLabels = variables;
        out.cell.resize(static_cast<std::size_t>(n));
        for (qsizetype i = 0; i < k; ++i)
        {
            for (qsizetype j = 0; j < k; ++j)
            {
                out.cell[static_cast<std::size_t>(i * k + j)] = static_cast<int>(j * k + i);
            }
        }
    }
    return out;
}

DiagnosticsView::DiagnosticsView(QWidget *parent) : QWidget(parent)
{
    title_ = new QLabel(tr("Select a dataset under /posterior in the navigator."));
    title_->setObjectName(QStringLiteral("viewTitle"));

    mode_ = new QComboBox;
    mode_->addItem(tr("One parameter"));
    mode_->addItem(tr("Whole block"));
    mode_->setToolTip(tr("One parameter's trace and posterior, or every parameter of the block at once, laid out "
                         "as its matrix: an equation per row and a regressor per column, or the variables of a "
                         "covariance against each other."));
    connect(mode_, &QComboBox::currentIndexChanged, this, &DiagnosticsView::modeChanged);

    row_ = new QComboBox;
    row_->setMinimumContentsLength(12);
    row_->setToolTip(tr("The parameter of the block whose draws are shown."));
    connect(row_, &QComboBox::currentIndexChanged, this, &DiagnosticsView::showRow);
    rowLabel_ = new QLabel(tr("Parameter"));

    stats_ = new QLabel;
    stats_->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto *head = new QHBoxLayout;
    head->addWidget(title_, 1);
    head->addWidget(mode_);
    head->addWidget(rowLabel_);
    head->addWidget(row_);

    trace_ = new QwtPlot;
    density_ = new QwtPlot;
    styleAxes(trace_, tr("Draw"), tr("Value"));
    styleAxes(density_, tr("Value"), tr("Share of draws"));
    trace_->setTitle(tr("Trace"));
    density_->setTitle(tr("Marginal posterior"));
    density_->setToolTip(tr("A histogram of the kept draws: the marginal posterior of the parameter. Right-click to copy or save the figure."));
    trace_->setToolTip(tr("The kept draws in order, the dashed line their mean. A chain that has converged wanders around one level with no trend. Right-click to copy or save the figure."));
    // Both panels side by side, as the view shows them.
    const FigureProvider figure = [this]() {
        FigureSpec spec;
        spec.title = row_->currentText();
        spec.columns = 2;
        spec.cells = {{trace_, {}}, {density_, {}}};
        spec.name = QStringLiteral("draws-%1").arg(row_->currentText());
        spec.heightMm = 65.0;
        return spec;
    };
    attachFigureExport(trace_, figure);
    attachFigureExport(density_, figure);

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
    histogram_->setPen(QPen(Qt::NoPen));
    histogram_->attach(density_);

    auto *plots = new QSplitter(Qt::Horizontal);
    plots->addWidget(trace_);
    plots->addWidget(density_);
    plots->setStretchFactor(0, 3);
    plots->setStretchFactor(1, 2);
    auto *onePage = new QWidget;
    {
        auto *l = new QVBoxLayout(onePage);
        l->setContentsMargins(0, 0, 0, 0);
        l->addWidget(plots, 1);
        l->addWidget(stats_);
    }

    // The whole block.
    show_ = new QComboBox;
    show_->addItem(tr("Marginal posteriors"));
    show_->addItem(tr("Traces"));
    show_->setToolTip(tr("What each cell of the matrix shows: the parameter's posterior, a histogram of its draws "
                         "with zero marked, or its trace."));
    connect(show_, &QComboBox::currentIndexChanged, this, [this]() {
        blockBuilt_ = false;
        buildBlock();
    });
    period_ = new QComboBox;
    period_->setToolTip(tr("The period at which a time-varying block is shown: each cell the posterior of that "
                           "coefficient in that period."));
    period_->setMaxVisibleItems(24);
    connect(period_, &QComboBox::currentIndexChanged, this, [this]() {
        blockBuilt_ = false;
        buildBlock();
    });
    periodLabel_ = new QLabel(tr("Period"));
    blockNote_ = new QLabel;
    blockNote_->setObjectName(QStringLiteral("subtle"));
    blockNote_->setWordWrap(true);
    QToolButton *exportBlock = figureExportButton([this]() {
        FigureSpec spec;
        spec.title = pathCoefficients_ > 0 ? tr("%1 at %2").arg(path_, period_->currentText()) : path_;
        spec.columns = layout_.columns();
        spec.columnLabels = layout_.columnLabels;
        spec.rowLabels = layout_.rowLabels;
        for (std::size_t i = 0; i < cells_.size(); ++i)
        {
            QString text;
            if (cells_[i] == nullptr && layout_.cell[i] >= 0)
            {
                // A fixed value: the label the grid shows in its place.
                const int r = static_cast<int>(i) / layout_.columns();
                const int c = static_cast<int>(i) % layout_.columns();
                if (auto *label = qobject_cast<QLabel *>(
                        gridLayout_->itemAtPosition(r + 1, c + 1) ? gridLayout_->itemAtPosition(r + 1, c + 1)->widget()
                                                                  : nullptr))
                {
                    text = label->text();
                }
            }
            spec.cells.push_back({cells_[i], text});
        }
        spec.name = QStringLiteral("block-%1").arg(QString(path_).replace(QLatin1Char('/'), QLatin1Char('-')));
        return spec;
    });
    auto *blockBar = new QHBoxLayout;
    blockBar->addWidget(new QLabel(tr("Each cell")));
    blockBar->addWidget(show_);
    blockBar->addWidget(periodLabel_);
    blockBar->addWidget(period_);
    blockBar->addWidget(blockNote_, 1);
    blockBar->addWidget(exportBlock);
    grid_ = new QWidget;
    gridLayout_ = new QGridLayout(grid_);
    gridLayout_->setSpacing(2);
    auto *scroll = new QScrollArea;
    scroll->setWidget(grid_);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *blockPage = new QWidget;
    {
        auto *l = new QVBoxLayout(blockPage);
        l->setContentsMargins(0, 0, 0, 0);
        l->addLayout(blockBar);
        l->addWidget(scroll, 1);
    }

    pages_ = new QStackedWidget;
    pages_->addWidget(onePage);
    pages_->addWidget(blockPage);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(head);
    layout->addWidget(pages_, 1);
}

void DiagnosticsView::clear()
{
    matrix_ = {};
    path_.clear();
    names_.clear();
    layout_ = {};
    pathCoefficients_ = 0;
    {
        const QSignalBlocker block(period_);
        period_->clear();
    }
    period_->setVisible(false);
    periodLabel_->setVisible(false);
    title_->setText(tr("Select a dataset under /posterior in the navigator."));
    row_->clear();
    stats_->clear();
    traceCurve_->setSamples(QVector<QPointF>{});
    meanCurve_->setSamples(QVector<QPointF>{});
    histogram_->setSamples(QVector<QwtIntervalSample>{});
    trace_->replot();
    density_->replot();
    clearBlock();
}

void DiagnosticsView::clearBlock()
{
    while (QLayoutItem *item = gridLayout_->takeAt(0))
    {
        delete item->widget();
        delete item;
    }
    // The stretches of the block before, which would hold its columns open.
    for (int c = 0; c < gridLayout_->columnCount(); ++c)
    {
        gridLayout_->setColumnStretch(c, 0);
    }
    for (int r = 0; r < gridLayout_->rowCount(); ++r)
    {
        gridLayout_->setRowStretch(r, 0);
    }
    cells_.clear();
    cellRows_.clear();
    blockBuilt_ = false;
}

void DiagnosticsView::showDataset(const QString &file, const QString &path, const QStringList &rowNames,
                                  const QStringList &variables, int periods)
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
    names_ = rowNames;
    title_->setText(tr("%1 — %2 parameters × %3 draws")
                        .arg(path).arg(matrix_.rows).arg(matrix_.cols));
    const auto n = static_cast<std::size_t>(rowNames.size());
    // A path is periods times a block: the named coefficients, or unnamed a
    // k x k covariance or a diagonal of k. The constant block comes first.
    std::size_t per = 0;
    if (periods > 1 && !(n > 0 && matrix_.rows == n) && matrix_.rows % static_cast<std::size_t>(periods) == 0)
    {
        const std::size_t candidate = matrix_.rows / static_cast<std::size_t>(periods);
        if (n > 0 ? candidate == n : blockLayout(candidate, {}, variables).valid())
        {
            per = candidate;
        }
    }
    const bool isPath = per > 0;
    const QStringList pathNames = n == per ? rowNames : QStringList();
    QStringList periodNames;
    if (isPath)
    {
        // Row t*per + i is element i in period t.
        pathCoefficients_ = static_cast<int>(per);
        periodNames = samplePeriodLabels(file, periods);
        layout_ = blockLayout(per, pathNames, variables);
        const QSignalBlocker block(period_);
        period_->addItems(periodNames);
        period_->setCurrentIndex(static_cast<int>(periodNames.size()) - 1);   // the last: where a forecast starts
        period_->setVisible(true);
        periodLabel_->setVisible(true);
    }
    else
    {
        layout_ = blockLayout(matrix_.rows, rowNames, variables);
    }

    const QSignalBlocker block(row_);
    for (std::size_t r = 0; r < matrix_.rows; ++r)
    {
        if (isPath)
        {
            const auto i = static_cast<qsizetype>(r % per);
            row_->addItem(tr("%1 — %2").arg(i < pathNames.size() ? pathNames[i] : tr("Element %1").arg(i + 1),
                                            periodNames.value(static_cast<qsizetype>(r / per))));
        }
        else
        {
            row_->addItem(r < n ? rowNames[static_cast<qsizetype>(r)] : tr("Row %1").arg(r + 1));
        }
    }
    showRow(0);
    modeChanged();
}

void DiagnosticsView::modeChanged()
{
    const bool whole = mode_->currentIndex() == 1;
    pages_->setCurrentIndex(whole ? 1 : 0);
    row_->setVisible(!whole);
    rowLabel_->setVisible(!whole);
    if (whole)
    {
        buildBlock();
    }
}

void DiagnosticsView::buildBlock()
{
    if (blockBuilt_)
    {
        return;
    }
    clearBlock();
    blockBuilt_ = true;
    if (matrix_.rows == 0)
    {
        blockNote_->setText(QString());
        return;
    }
    if (!layout_.valid())
    {
        blockNote_->setText(tr("This block has no matrix layout to show it in: its rows are not named by "
                               "equation and regressor, nor is it k × k."));
        return;
    }
    if (layout_.rows() * layout_.columns() > maxCells)
    {
        blockNote_->setText(tr("%1 × %2 is more cells than can be read at once; choose one parameter instead.")
                                .arg(layout_.rows())
                                .arg(layout_.columns()));
        return;
    }
    const bool traces = show_->currentIndex() == 1;
    blockNote_->setText(traces ? tr("Each cell is a parameter's trace. Click one to see it on its own.")
                               : tr("Each cell is a parameter's posterior, zero marked where it is in range. "
                                    "Click one to see it on its own."));

    for (int c = 0; c < layout_.columns(); ++c)
    {
        gridLayout_->addWidget(header(layout_.columnLabels[c], Qt::AlignHCenter | Qt::AlignBottom), 0, c + 1);
    }
    QFont small = font();
    small.setPointSizeF(small.pointSizeF() * 0.8);
    cells_.assign(layout_.cell.size(), nullptr);
    for (int r = 0; r < layout_.rows(); ++r)
    {
        gridLayout_->addWidget(header(layout_.rowLabels[r], Qt::AlignRight | Qt::AlignVCenter), r + 1, 0);
        for (int c = 0; c < layout_.columns(); ++c)
        {
            const int cellIndex = layout_.cell[static_cast<std::size_t>(r * layout_.columns() + c)];
            if (cellIndex < 0)
            {
                continue;
            }
            // A path's coefficient at the chosen period.
            const int index = pathCoefficients_ > 0
                                  ? std::max(0, period_->currentIndex()) * pathCoefficients_ + cellIndex
                                  : cellIndex;
            const auto row = static_cast<std::size_t>(index);
            std::vector<double> draws = rowOf(matrix_, row);
            std::vector<double> sorted = draws;
            std::sort(sorted.begin(), sorted.end());

            if (!sorted.empty() && sorted.front() == sorted.back())
            {
                auto *fixed = new QLabel(QString::number(sorted.front(), 'g', 4));
                fixed->setAlignment(Qt::AlignCenter);
                fixed->setObjectName(QStringLiteral("subtle"));
                fixed->setToolTip(tr("Fixed at %1 in every draw: not estimated.").arg(sorted.front()));
                gridLayout_->addWidget(fixed, r + 1, c + 1);
                continue;
            }
            auto *plot = new QwtPlot;
            styleAxes(plot, QString(), QString());
            plot->enableAxis(QwtPlot::yLeft, false);
            plot->setAxisFont(QwtPlot::xBottom, small);
            plot->setAxisMaxMajor(QwtPlot::xBottom, 3);
            plot->setMinimumSize(120, 84);
            if (traces)
            {
                // At most a thousand points a cell: the shape, not every draw.
                const std::size_t step = std::max<std::size_t>(1, draws.size() / 1000);
                QVector<QPointF> points;
                for (std::size_t d = 0; d < draws.size(); d += step)
                {
                    points.append({static_cast<double>(d + 1), draws[d]});
                }
                auto *curve = new QwtPlotCurve;
                curve->setPen(seriesColor(0), 0.8);
                curve->setSamples(points);
                curve->attach(plot);
                plot->enableAxis(QwtPlot::yLeft, true);
                plot->setAxisFont(QwtPlot::yLeft, small);
                plot->setAxisMaxMajor(QwtPlot::yLeft, 3);
                plot->enableAxis(QwtPlot::xBottom, false);
            }
            else
            {
                auto *hist = new QwtPlotHistogram;
                // An outline, not columns: framed bars clutter a small cell.
                hist->setStyle(QwtPlotHistogram::Outline);
                hist->setBrush(seriesColor(0, 160));
                hist->setPen(QPen(seriesColor(0), 0.8));
                hist->setSamples(histogramOf(sorted, 10, 40));
                hist->attach(plot);
                if (!sorted.empty() && sorted.front() < 0.0 && sorted.back() > 0.0)
                {
                    auto *zero = new QwtPlotMarker;
                    zero->setLineStyle(QwtPlotMarker::VLine);
                    zero->setXValue(0.0);
                    zero->setLinePen(seriesColor(1), 1.6, Qt::DashLine);   // thin lines fade at fractional pixels
                    zero->setZ(hist->z() + 1.0);   // over the bars, not tinted by them
                    zero->attach(plot);
                }
            }
            const double n = static_cast<double>(draws.size());
            const double mean = std::accumulate(draws.begin(), draws.end(), 0.0) / n;
            double ss = 0.0;
            for (const double d : draws)
            {
                ss += (d - mean) * (d - mean);
            }
            const QString name =
                pathCoefficients_ > 0
                    ? tr("%1 — %2")
                          .arg(cellIndex < names_.size() && names_.size() == pathCoefficients_
                                   ? names_[cellIndex]
                                   : QStringLiteral("%1, %2").arg(layout_.rowLabels[r], layout_.columnLabels[c]),
                               period_->currentText())
                : index < names_.size()
                    ? names_[index]
                    : QStringLiteral("%1, %2").arg(layout_.rowLabels[r], layout_.columnLabels[c]);
            plot->setToolTip(tr("%1\nmean %2, sd %3, 90% interval [%4, %5]\nClick to see it on its own.")
                                 .arg(name)
                                 .arg(mean, 0, 'g', 4)
                                 .arg(n > 1 ? std::sqrt(ss / (n - 1.0)) : 0.0, 0, 'g', 4)
                                 .arg(quantile(sorted, 0.05), 0, 'g', 4)
                                 .arg(quantile(sorted, 0.95), 0, 'g', 4));
            plot->canvas()->installEventFilter(this);
            plot->canvas()->setCursor(Qt::PointingHandCursor);
            cellRows_.insert(plot->canvas(), index);
            plot->replot();
            gridLayout_->addWidget(plot, r + 1, c + 1);
            cells_[static_cast<std::size_t>(r * layout_.columns() + c)] = plot;
        }
    }
    for (int c = 0; c < layout_.columns(); ++c)
    {
        gridLayout_->setColumnStretch(c + 1, 1);
    }
    for (int r = 0; r < layout_.rows(); ++r)
    {
        gridLayout_->setRowStretch(r + 1, 1);
    }
}

bool DiagnosticsView::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonRelease &&
        static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton && cellRows_.contains(watched))
    {
        // A cell clicked: that parameter, on its own.
        row_->setCurrentIndex(cellRows_.value(watched));
        mode_->setCurrentIndex(0);
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

void DiagnosticsView::showRow(int row)
{
    if (row < 0 || static_cast<std::size_t>(row) >= matrix_.rows || matrix_.cols == 0)
    {
        return;
    }
    const auto r = static_cast<std::size_t>(row);

    const std::vector<double> draws = rowOf(matrix_, r);
    QVector<QPointF> trace(static_cast<qsizetype>(matrix_.cols));
    for (std::size_t c = 0; c < matrix_.cols; ++c)
    {
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
    histogram_->setSamples(histogramOf(sorted, 10, 80));

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
