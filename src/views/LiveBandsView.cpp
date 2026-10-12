#include "views/LiveBandsView.h"

#include "engine/ModelFileTree.h"
#include "engine/QtReporter.h"
#include "views/FigureExport.h"
#include "views/PlotStyle.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPen>
#include <QComboBox>
#include <QTimer>
#include <QVBoxLayout>

#include <qwt_interval.h>
#include <qwt_plot.h>
#include <qwt_plot_curve.h>
#include <qwt_plot_intervalcurve.h>
#include <qwt_samples.h>

#include <algorithm>
#include <cmath>

namespace studio
{

namespace
{

double quantileOfSorted(const std::vector<double> &sorted, double q)
{
    const double pos = q * static_cast<double>(sorted.size() - 1);
    const auto lo = static_cast<std::size_t>(std::floor(pos));
    const auto hi = std::min(lo + 1, sorted.size() - 1);
    return sorted[lo] + (pos - static_cast<double>(lo)) * (sorted[hi] - sorted[lo]);
}

QwtPlotIntervalCurve *band(const QColor &color)
{
    auto *curve = new QwtPlotIntervalCurve;
    curve->setStyle(QwtPlotIntervalCurve::Tube);
    curve->setPen(QPen(Qt::transparent, 0.0));
    curve->setBrush(color);
    curve->setRenderHint(QwtPlotItem::RenderAntialiased);
    return curve;
}

} // namespace

LiveBandsView::LiveBandsView(QtReporter *reporter, QWidget *parent)
    : QWidget(parent), reporter_(reporter)
{
    row_ = new QComboBox;
    row_->setMinimumContentsLength(28);
    row_->setMaxVisibleItems(24);
    row_->setToolTip(tr("The time-varying coefficient whose path is shown, as its bands form while the sampler runs."));
    row_->setEnabled(false);
    connect(row_, &QComboBox::currentIndexChanged, this, [this](int i) { coefficientChanged(i); });

    status_ = new QLabel(tr("Open a model with time-varying coefficients."));
    status_->setObjectName(QStringLiteral("subtle"));

    auto *header = new QHBoxLayout;
    header->addWidget(row_);
    header->addWidget(status_, 1);

    plot_ = new QwtPlot;
    styleAxes(plot_, tr("Period"), tr("Coefficient"));
    plot_->setToolTip(tr("The coefficient's path over the sample: the median of the draws kept so far with its bands, redrawn as the chain runs. Right-click to copy or save the figure."));
    attachFigureExport(plot_, [this]() {
        return singleFigure(plot_, tr("Time-varying coefficient %1").arg(row_->currentText()),
                            QStringLiteral("tvp-%1").arg(row_->currentText()));
    });
    band90_ = band(seriesColor(0, 50));
    band68_ = band(seriesColor(0, 95));
    band90_->attach(plot_);
    band68_->attach(plot_);
    median_ = new QwtPlotCurve;
    median_->setPen(seriesColor(0), 2.0);
    median_->setRenderHint(QwtPlotItem::RenderAntialiased);
    median_->attach(plot_);

    timer_ = new QTimer(this);
    timer_->setInterval(250);
    connect(timer_, &QTimer::timeout, this, &LiveBandsView::poll);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(header);
    layout->addWidget(plot_, 1);
}

void LiveBandsView::fillRows(int count)
{
    const QSignalBlocker block(row_);
    const int keep = std::max(0, row_->currentIndex());
    row_->clear();
    for (int i = 0; i < count; ++i)
    {
        row_->addItem(i < names_.size() ? names_[i] : tr("Coefficient %1").arg(i + 1));
    }
    row_->setCurrentIndex(std::min(keep, count - 1));
}

int LiveBandsView::currentRow() const
{
    return std::max(0, row_->currentIndex());
}

void LiveBandsView::setModel(bool timeVarying, int periods, int coefficients, const QStringList &names)
{
    timeVarying_ = timeVarying;
    periods_ = periods;
    names_ = names;
    if (coefficients == 0 && !names.isEmpty())
    {
        coefficients = static_cast<int>(names.size());
    }
    coefficients_ = coefficients;
    paths_.clear();
    row_->setEnabled(timeVarying);
    // Until the coefficients per period are known there is one entry; the
    // first kept draw of a run fills the rest in (see poll()).
    fillRows(coefficients > 0 ? coefficients : 1);
    if (!timeVarying)
    {
        status_->setText(tr("This model has no time-varying coefficient path."));
    }
    redraw();
}

void LiveBandsView::coefficientChanged(int row)
{
    paths_.clear();
    if (live_)
    {
        // Earlier draws of this row were not kept; the bands start again.
        reporter_->watch(row);
        redraw();
    }
    else if (!file_.isEmpty())
    {
        showStored(file_);
    }
}

void LiveBandsView::runStarted()
{
    live_ = timeVarying_;
    paths_.clear();
    if (live_)
    {
        reporter_->watch(currentRow());
        timer_->start();
    }
    redraw();
}

void LiveBandsView::runFinished()
{
    timer_->stop();
    poll();
    live_ = false;
    reporter_->watch(-1);
}

void LiveBandsView::poll()
{
    if (coefficients_ == 0 && reporter_->pathRows() > 0)
    {
        coefficients_ = reporter_->pathRows();
        fillRows(coefficients_);
    }
    std::vector<std::vector<double>> fresh = reporter_->takeDraws();
    if (fresh.empty())
    {
        return;
    }
    for (auto &path : fresh)
    {
        paths_.push_back(std::move(path));
    }
    redraw();
}

void LiveBandsView::showStored(const QString &file)
{
    file_ = file;
    paths_.clear();
    if (!timeVarying_ || coefficients_ <= 0 || live_)
    {
        redraw();
        return;
    }
    try
    {
        // /posterior/a/coeffs: rows are the path vectorised column-major from
        // the sampler's (coefficients x periods) matrix, columns are draws.
        const Matrix m = readMatrix(file, QStringLiteral("/posterior/a/coeffs"));
        const auto k = static_cast<std::size_t>(coefficients_);
        const std::size_t periods = m.rows / k;
        const auto row = static_cast<std::size_t>(currentRow());
        paths_.assign(m.cols, std::vector<double>(periods));
        for (std::size_t d = 0; d < m.cols; ++d)
        {
            for (std::size_t t = 0; t < periods; ++t)
            {
                paths_[d][t] = m.at(t * k + row, d);
            }
        }
    }
    catch (const std::exception &)
    {
        paths_.clear();
    }
    redraw();
}

void LiveBandsView::redraw()
{
    QVector<QwtIntervalSample> b90;
    QVector<QwtIntervalSample> b68;
    QVector<QPointF> med;

    if (!paths_.empty())
    {
        const std::size_t periods = paths_.front().size();
        std::vector<double> column(paths_.size());
        for (std::size_t t = 0; t < periods; ++t)
        {
            for (std::size_t d = 0; d < paths_.size(); ++d)
            {
                column[d] = paths_[d][t];
            }
            std::sort(column.begin(), column.end());
            const double x = static_cast<double>(t + 1);
            b90.append({x, QwtInterval(quantileOfSorted(column, 0.05), quantileOfSorted(column, 0.95))});
            b68.append({x, QwtInterval(quantileOfSorted(column, 0.16), quantileOfSorted(column, 0.84))});
            med.append({x, quantileOfSorted(column, 0.5)});
        }
    }
    band90_->setSamples(b90);
    band68_->setSamples(b68);
    median_->setSamples(med);

    if (timeVarying_)
    {
        const QString what = live_ ? tr("Sampling — %1 kept draws so far").arg(paths_.size())
                                   : paths_.empty() ? tr("No draws yet. Run the model to watch the bands form.")
                                                    : tr("%1 draws").arg(paths_.size());
        status_->setText(tr("Median, 68% and 90% bands.  %1").arg(what));
    }
    plot_->replot();
}

} // namespace studio
