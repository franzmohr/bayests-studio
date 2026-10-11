#include "views/SummaryPlotView.h"

#include "views/PlotStyle.h"
#include "views/SeriesPlot.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPen>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <qwt_interval.h>
#include <qwt_plot.h>
#include <qwt_plot_curve.h>
#include <qwt_plot_histogram.h>
#include <qwt_plot_intervalcurve.h>
#include <qwt_plot_marker.h>
#include <qwt_plot_tradingcurve.h>
#include <qwt_samples.h>
#include <qwt_symbol.h>

#include <algorithm>
#include <cmath>

namespace studio
{

namespace
{

/// At most this many regressor columns per page, bvartools' max_cols.
constexpr int MaxColumns = 6;

enum PanelType
{
    Histogram,
    Trace,
    BoxPlot,
};

QLabel *subtle(const QString &text = {})
{
    auto *label = new QLabel(text);
    label->setObjectName(QStringLiteral("subtle"));
    label->setWordWrap(true);
    return label;
}

/// R's pretty() step for about `n` intervals over `range`.
double prettyStep(double range, int n)
{
    const double raw = range / std::max(1, n);
    const double magnitude = std::pow(10.0, std::floor(std::log10(raw)));
    for (const double f : {1.0, 2.0, 5.0, 10.0})
    {
        if (f * magnitude >= raw * (1.0 - 1e-9))
        {
            return f * magnitude;
        }
    }
    return 10.0 * magnitude;
}

QwtPlot *smallPlot()
{
    auto *plot = new QwtPlot;
    styleAxes(plot, QString(), QString());
    QFont f = plot->axisFont(QwtPlot::xBottom);
    f.setPointSizeF(f.pointSizeF() * 0.8);
    plot->setAxisFont(QwtPlot::xBottom, f);
    plot->setAxisFont(QwtPlot::yLeft, f);
    plot->setAxisMaxMajor(QwtPlot::xBottom, 4);
    plot->setAxisMaxMajor(QwtPlot::yLeft, 4);
    plot->setMinimumSize(150, 120);
    return plot;
}

void zeroLine(QwtPlot *plot)
{
    auto *zero = new QwtPlotMarker;
    zero->setLineStyle(QwtPlotMarker::HLine);
    zero->setYValue(0.0);
    QColor c = plot->palette().color(QPalette::Text);
    c.setAlpha(110);
    zero->setLinePen(c, 1.0);
    zero->attach(plot);
}

/// R's hist(): Sturges' number of classes on pretty breaks.
void histogram(QwtPlot *plot, const arma::vec &draws)
{
    const double lo = draws.min();
    const double hi = draws.max();
    const int classes = static_cast<int>(std::ceil(std::log2(static_cast<double>(draws.n_elem)) + 1.0));
    const double step = prettyStep(hi - lo, classes);
    const double first = std::floor(lo / step) * step;
    const int bins = std::max(1, static_cast<int>(std::ceil((hi - first) / step - 1e-9)));
    std::vector<double> counts(static_cast<std::size_t>(bins), 0.0);
    for (const double v : draws)
    {
        const int b = std::clamp(static_cast<int>(std::floor((v - first) / step)), 0, bins - 1);
        counts[static_cast<std::size_t>(b)] += 1.0;
    }
    QVector<QwtIntervalSample> samples;
    for (int b = 0; b < bins; ++b)
    {
        samples.append({counts[static_cast<std::size_t>(b)], first + b * step, first + (b + 1) * step});
    }
    auto *h = new QwtPlotHistogram;
    h->setStyle(QwtPlotHistogram::Columns);
    h->setBrush(seriesColor(0, 150));
    h->setPen(QPen(seriesColor(0), 0.8));
    h->setSamples(samples);
    h->attach(plot);
    plot->setAxisScale(QwtPlot::xBottom, first, first + bins * step);
}

void trace(QwtPlot *plot, const arma::vec &draws)
{
    QVector<QPointF> points;
    points.reserve(static_cast<qsizetype>(draws.n_elem));
    for (arma::uword d = 0; d < draws.n_elem; ++d)
    {
        points.append({double(d + 1), draws(d)});
    }
    auto *c = new QwtPlotCurve;
    c->setPen(seriesColor(0), 0.6);
    c->setSamples(points);
    c->attach(plot);
    plot->setAxisScale(QwtPlot::xBottom, 1.0, double(std::max<arma::uword>(2, draws.n_elem)));
}

/// R's boxplot(): the quartiles, whiskers to the most extreme draw within 1.5
/// interquartile ranges, and the draws beyond as points.
void boxPlot(QwtPlot *plot, const arma::vec &draws)
{
    std::vector<double> v(draws.begin(), draws.end());
    std::sort(v.begin(), v.end());
    // boxplot.stats() uses Tukey's hinges, which for this purpose are the
    // quartiles of fivenum().
    const auto fivenumAt = [&](double pos) {
        const auto lo = static_cast<std::size_t>(std::floor(pos));
        const auto hi = static_cast<std::size_t>(std::ceil(pos));
        return 0.5 * (v[lo] + v[hi]);
    };
    const double n = static_cast<double>(v.size());
    const double n4 = std::floor((n + 3.0) / 2.0) / 2.0;
    const double q1 = fivenumAt(n4 - 1.0);
    const double med = fivenumAt((n + 1.0) / 2.0 - 1.0);
    const double q3 = fivenumAt(n - n4);
    const double reach = 1.5 * (q3 - q1);
    double wLow = q1, wHigh = q3;
    QVector<QPointF> outliers;
    for (const double x : v)
    {
        if (x < q1 - reach || x > q3 + reach)
        {
            outliers.append({0.0, x});
        }
        else
        {
            wLow = std::min(wLow, x);
            wHigh = std::max(wHigh, x);
        }
    }
    auto *box = new QwtPlotTradingCurve;
    box->setSymbolStyle(QwtPlotTradingCurve::CandleStick);
    box->setSymbolExtent(0.5);
    box->setSymbolPen(QPen(plot->palette().color(QPalette::Text), 1.0));
    box->setSymbolBrush(QwtPlotTradingCurve::Increasing, seriesColor(0, 120));
    box->setSymbolBrush(QwtPlotTradingCurve::Decreasing, seriesColor(0, 120));
    box->setSamples(QVector<QwtOHLCSample>{QwtOHLCSample(0.0, q1, wHigh, wLow, q3)});
    box->attach(plot);
    auto *median = new QwtPlotCurve;
    median->setPen(plot->palette().color(QPalette::Text), 2.0);
    median->setSamples(QVector<QPointF>{{-0.25, med}, {0.25, med}});
    median->attach(plot);
    if (!outliers.isEmpty())
    {
        auto *dots = new QwtPlotCurve;
        dots->setStyle(QwtPlotCurve::NoCurve);
        dots->setSymbol(new QwtSymbol(QwtSymbol::Ellipse, Qt::NoBrush, QPen(seriesColor(0), 0.8), QSize(5, 5)));
        dots->setSamples(outliers);
        dots->attach(plot);
    }
    plot->setAxisScale(QwtPlot::xBottom, -1.0, 1.0);
    plot->enableAxis(QwtPlot::xBottom, false);
}

void path(QwtPlot *plot, const arma::mat &bands, double start, double frequency)
{
    QVector<QwtIntervalSample> band;
    QVector<QPointF> median;
    for (arma::uword t = 0; t < bands.n_rows; ++t)
    {
        const double x = start + static_cast<double>(t) / frequency;
        band.append({x, QwtInterval(bands(t, 0), bands(t, 2))});
        median.append({x, bands(t, 1)});
    }
    auto *tube = new QwtPlotIntervalCurve;
    tube->setStyle(QwtPlotIntervalCurve::Tube);
    tube->setPen(QPen(Qt::transparent, 0.0));
    tube->setBrush(seriesColor(0, 70));
    tube->setSamples(band);
    tube->attach(plot);
    auto *line = new QwtPlotCurve;
    line->setPen(seriesColor(0), 1.5);
    line->setRenderHint(QwtPlotItem::RenderAntialiased);
    line->setSamples(median);
    line->attach(plot);
    zeroLine(plot);
    plot->setAxisScale(QwtPlot::xBottom, start, start + static_cast<double>(bands.n_rows - 1) / frequency);
}

QLabel *headerLabel(const QString &text, bool vertical = false)
{
    auto *l = new QLabel(text);
    l->setAlignment(Qt::AlignCenter);
    QFont f = l->font();
    f.setBold(true);
    l->setFont(f);
    if (vertical)
    {
        l->setMinimumWidth(70);
    }
    return l;
}

} // namespace

SummaryPlotView::SummaryPlotView(QWidget *parent) : QWidget(parent)
{
    status_ = subtle();
    page_ = new QComboBox;
    page_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    page_->setObjectName(QStringLiteral("summaryBlock"));
    type_ = new QComboBox;
    type_->addItem(tr("Histogram"), Histogram);
    type_->addItem(tr("Trace"), Trace);
    type_->addItem(tr("Box plot"), BoxPlot);
    type_->setObjectName(QStringLiteral("summaryType"));
    ci_ = new QDoubleSpinBox;
    ci_->setRange(0.5, 0.99);
    ci_->setSingleStep(0.05);
    ci_->setDecimals(2);
    ci_->setValue(0.95);
    ci_->setToolTip(tr("The probability of the credible band drawn for a coefficient that moves."));

    auto *controls = new QHBoxLayout;
    controls->addWidget(new QLabel(tr("Block")));
    controls->addWidget(page_);
    controls->addSpacing(12);
    controls->addWidget(new QLabel(tr("Draws as")));
    controls->addWidget(type_);
    controls->addSpacing(12);
    controls->addWidget(new QLabel(tr("Band")));
    controls->addWidget(ci_);
    controls->addStretch(1);

    title_ = new QLabel;
    title_->setAlignment(Qt::AlignCenter);
    title_->setObjectName(QStringLiteral("sectionTitle"));

    gridHost_ = new QWidget;
    grid_ = new QGridLayout(gridHost_);
    grid_->setSpacing(6);
    auto *scroll = new QScrollArea;
    scroll->setWidget(gridHost_);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 8, 0, 0);
    layout->addLayout(controls);
    layout->addWidget(status_);
    layout->addWidget(title_);
    layout->addWidget(scroll, 1);

    watcher_ = new QFutureWatcher<Result>(this);
    connect(watcher_, &QFutureWatcher<Result>::finished, this, [this]() {
        const Result r = watcher_->result();
        if (pending_)
        {
            pending_ = false;
            compute();
            return;
        }
        if (!r.error.isEmpty())
        {
            summary_.reset();
            status_->setText(tr("Not available for this model: %1").arg(r.error));
            rebuildPages();
            return;
        }
        summary_ = r.summary;
        rebuildPages();
    });
    connect(page_, &QComboBox::currentIndexChanged, this, &SummaryPlotView::showPage);
    connect(type_, &QComboBox::currentIndexChanged, this, &SummaryPlotView::showPage);
    connect(ci_, &QDoubleSpinBox::valueChanged, this, &SummaryPlotView::compute);
    setModel(QString());
}

void SummaryPlotView::setModel(const QString &file)
{
    file_ = file;
    if (file.isEmpty())
    {
        summary_.reset();
        status_->setText(tr("Select a model with posterior draws."));
        rebuildPages();
        return;
    }
    compute();
}

void SummaryPlotView::compute()
{
    if (file_.isEmpty())
    {
        return;
    }
    if (watcher_->isRunning())
    {
        pending_ = true;
        return;
    }
    status_->setText(tr("Reading the draws…"));
    const QString file = file_;
    const double ci = ci_->value();
    watcher_->setFuture(QtConcurrent::run([file, ci]() {
        Result r;
        try
        {
            r.summary = std::make_shared<CoefficientSummary>(coefficientSummary(file, ci));
        }
        catch (const std::exception &e)
        {
            r.error = QString::fromUtf8(e.what());
        }
        return r;
    }));
}

void SummaryPlotView::rebuildPages()
{
    const QString previous = page_->currentText();
    {
        const QSignalBlocker block(page_);
        page_->clear();
        if (summary_)
        {
            for (int b = 0; b < static_cast<int>(summary_->blocks.size()); ++b)
            {
                const int columns = static_cast<int>(summary_->blocks[static_cast<std::size_t>(b)].columns.size());
                // Pages of nearly equal width: seven columns become four and three.
                const int pages = std::max(1, (columns + MaxColumns - 1) / MaxColumns);
                for (int pg = 0; pg < pages; ++pg)
                {
                    const QString name = summary_->blocks[static_cast<std::size_t>(b)].title;
                    page_->addItem(pages > 1 ? tr("%1 (%2 of %3)").arg(name).arg(pg + 1).arg(pages) : name,
                                   QVariant::fromValue(QPoint(b, pg)));
                }
            }
            const int again = page_->findText(previous);
            page_->setCurrentIndex(again >= 0 ? again : 0);
        }
    }
    page_->setEnabled(summary_ != nullptr);
    if (summary_)
    {
        status_->setText(tr("%1 kept draws; a row per equation, a column per regressor.").arg(summary_->draws));
    }
    showPage();
}

void SummaryPlotView::showPage()
{
    while (QLayoutItem *item = grid_->takeAt(0))
    {
        delete item->widget();
        delete item;
    }
    title_->clear();
    if (!summary_ || page_->currentIndex() < 0)
    {
        return;
    }
    const QPoint at = page_->currentData().toPoint();
    const CoefficientBlock &block = summary_->blocks[static_cast<std::size_t>(at.x())];
    const int k = static_cast<int>(summary_->equations.size());
    const int columns = static_cast<int>(block.columns.size());
    const int pages = std::max(1, (columns + MaxColumns - 1) / MaxColumns);
    // Split as R's split(seq, sort(rep_len(1:pages, n))) does.
    const int base = columns / pages;
    const int extra = columns % pages;
    const int first = at.y() * base + std::min(at.y(), extra);
    const int count = base + (at.y() < extra ? 1 : 0);

    title_->setText(QStringLiteral("%1\n%2").arg(summary_->title, page_->currentText()));
    const auto type = static_cast<PanelType>(type_->currentData().toInt());
    bool anyDraws = false;
    for (int c = 0; c < count; ++c)
    {
        grid_->addWidget(headerLabel(block.columns.value(first + c)), 0, c + 1);
    }
    for (int i = 0; i < k; ++i)
    {
        grid_->addWidget(headerLabel(summary_->equations.value(i), true), i + 1, 0);
        for (int c = 0; c < count; ++c)
        {
            const CoefficientPanel &p = block.panels[static_cast<std::size_t>((first + c) * k + i)];
            if (p.kind == CoefficientPanel::Kind::Fixed)
            {
                auto *value = new QLabel(QString::number(p.value, 'g', 6));
                value->setAlignment(Qt::AlignCenter);
                value->setMinimumSize(150, 120);
                grid_->addWidget(value, i + 1, c + 1);
                continue;
            }
            QwtPlot *plot = smallPlot();
            if (p.kind == CoefficientPanel::Kind::Path)
            {
                path(plot, p.path, summary_->start, summary_->frequency);
            }
            else
            {
                anyDraws = true;
                switch (type)
                {
                case Histogram: histogram(plot, p.draws); break;
                case Trace: trace(plot, p.draws); break;
                case BoxPlot: boxPlot(plot, p.draws); break;
                }
            }
            plot->replot();
            grid_->addWidget(plot, i + 1, c + 1);
        }
    }
    for (int c = 1; c <= count; ++c)
    {
        grid_->setColumnStretch(c, 1);
    }
    type_->setEnabled(anyDraws);
}

} // namespace studio
