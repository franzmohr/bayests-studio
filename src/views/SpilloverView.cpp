#include "views/SpilloverView.h"

#include "engine/ModelFileTree.h"
#include "views/PlotStyle.h"
#include "views/SeriesPlot.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPen>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <qwt_column_symbol.h>
#include <qwt_interval.h>
#include <qwt_interval_symbol.h>
#include <qwt_legend.h>
#include <qwt_plot.h>
#include <qwt_plot_barchart.h>
#include <qwt_plot_curve.h>
#include <qwt_plot_intervalcurve.h>
#include <qwt_plot_marker.h>
#include <qwt_samples.h>
#include <qwt_scale_draw.h>
#include <qwt_text.h>

#include <algorithm>
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

/// Variable names under the bars.
class NameScaleDraw : public QwtScaleDraw
{
public:
    explicit NameScaleDraw(QStringList names) : names_(std::move(names))
    {
        enableComponent(QwtScaleDraw::Ticks, false);
        enableComponent(QwtScaleDraw::Backbone, false);
    }
    QwtText label(double value) const override
    {
        const int i = static_cast<int>(std::lround(value));
        return std::abs(value - i) < 1e-6 && i >= 0 && i < names_.size() ? QwtText(names_[i]) : QwtText();
    }

private:
    QStringList names_;
};

/// Median and the 5% and 95% quantiles of one column of draws.
struct Summary
{
    double low, median, high;
};

Summary summarise(const arma::vec &draws)
{
    std::vector<double> v(draws.begin(), draws.end());
    std::sort(v.begin(), v.end());
    return {sortedQuantile(v, 0.05), sortedQuantile(v, 0.5), sortedQuantile(v, 0.95)};
}

QString percent(double v)
{
    return QString::number(v, 'f', 1);
}

QTableWidget *makeTable()
{
    auto *t = new QTableWidget;
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setSelectionMode(QAbstractItemView::NoSelection);
    t->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    t->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    t->setFocusPolicy(Qt::NoFocus);
    return t;
}

QTableWidgetItem *cell(const QString &text, const QColor &background = {}, bool bold = false)
{
    auto *item = new QTableWidgetItem(text);
    item->setTextAlignment(Qt::AlignCenter);
    if (background.isValid())
    {
        item->setBackground(background);
    }
    if (bold)
    {
        QFont f = item->font();
        f.setBold(true);
        item->setFont(f);
    }
    return item;
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

} // namespace

SpilloverView::SpilloverView(QWidget *parent) : QWidget(parent)
{
    status_ = subtle();
    type_ = new QComboBox;
    type_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    horizon_ = new QSpinBox;
    horizon_->setRange(1, 200);
    horizon_->setValue(10);
    horizon_->setToolTip(tr("H: the forecast error variance decomposed is that of the H step forecast, "
                            "made of the responses of periods 0 to H - 1."));
    period_ = new QSpinBox;
    period_->setMinimum(1);
    periodLabel_ = new QLabel(tr("Period"));

    auto *controls = new QHBoxLayout;
    controls->addWidget(new QLabel(tr("Decomposition")));
    controls->addWidget(type_);
    controls->addSpacing(12);
    controls->addWidget(new QLabel(tr("Horizon H")));
    controls->addWidget(horizon_);
    controls->addSpacing(12);
    controls->addWidget(periodLabel_);
    controls->addWidget(period_);
    controls->addStretch(1);

    // The table as the literature prints it.
    auto *tablePage = new QWidget;
    auto *tl = new QVBoxLayout(tablePage);
    tl->setContentsMargins(0, 8, 0, 0);
    tl->addWidget(subtle(tr("Row j, column k: the share of the forecast error variance of j due to shocks "
                            "to k, in percent, posterior mean. The last column is what each variable "
                            "receives from the others, the last row what it transmits to them, and the "
                            "corner the total spillover index.")));
    table_ = makeTable();
    tl->addWidget(table_, 1);

    // Directional measures with their intervals.
    auto *barsPage = new QWidget;
    auto *bl = new QVBoxLayout(barsPage);
    bl->setContentsMargins(0, 8, 0, 0);
    measure_ = new QComboBox;
    measure_->addItem(tr("Net: to others less from others"));
    measure_->addItem(tr("To others"));
    measure_->addItem(tr("From others"));
    auto *pick = new QHBoxLayout;
    pick->addWidget(new QLabel(tr("Measure")));
    pick->addWidget(measure_);
    pick->addStretch(1);
    bl->addLayout(pick);
    bl->addWidget(subtle(tr("Median with the 90% credible interval. A positive net value marks a variable "
                            "that transmits more forecast error variance than it receives.")));
    bars_ = new QwtPlot;
    styleAxes(bars_, QString(), tr("Percent"));
    bl->addWidget(bars_, 1);
    connect(measure_, &QComboBox::currentIndexChanged, this, &SpilloverView::showDirectional);

    // Net pairwise.
    auto *pairPage = new QWidget;
    auto *pl = new QVBoxLayout(pairPage);
    pl->setContentsMargins(0, 8, 0, 0);
    pl->addWidget(subtle(tr("Row j, column k: what k transmits to j less what j transmits to k, in percent, "
                            "posterior mean. Positive (green) where the column variable is the net "
                            "transmitter of the pair.")));
    pairwise_ = makeTable();
    pl->addWidget(pairwise_, 1);

    // The index over the sample.
    auto *pathPage = new QWidget;
    auto *ol = new QVBoxLayout(pathPage);
    ol->setContentsMargins(0, 8, 0, 0);
    pathNote_ = subtle();
    ol->addWidget(pathNote_);
    path_ = new QwtPlot;
    styleAxes(path_, tr("Period"), tr("Total spillover index, percent"));
    path_->insertLegend(new QwtLegend, QwtPlot::BottomLegend);
    ol->addWidget(path_, 1);

    tabs_ = new QTabWidget;
    tabs_->setDocumentMode(true);
    tabs_->addTab(tablePage, tr("Connectedness table"));
    tabs_->addTab(barsPage, tr("Directional"));
    tabs_->addTab(pairPage, tr("Net pairwise"));
    tabs_->addTab(pathPage, tr("Over time"));
    tabs_->setObjectName(QStringLiteral("spilloverTabs"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 8, 0, 0);
    layout->addLayout(controls);
    layout->addWidget(status_);
    layout->addWidget(tabs_, 1);

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
    connect(debounce_, &QTimer::timeout, this, &SpilloverView::compute);
    connect(type_, &QComboBox::currentIndexChanged, this, [this]() {
        period_->setEnabled(static_cast<SpilloverType>(type_->currentData().toInt()) != SpilloverType::Sign);
        schedule();
    });
    for (QSpinBox *s : {horizon_, period_})
    {
        connect(s, &QSpinBox::valueChanged, this, &SpilloverView::schedule);
    }
    setModel(QString());
}

void SpilloverView::setModel(const QString &file)
{
    file_ = file;
    support_ = file.isEmpty() ? SpilloverSupport{} : spilloverSupport(file);
    const bool ok = support_.ok;
    for (QWidget *w : std::initializer_list<QWidget *>{type_, horizon_, period_, tabs_})
    {
        w->setEnabled(ok);
    }
    if (!ok)
    {
        status_->setText(file.isEmpty() ? tr("Select a model of the project.")
                                        : tr("Not available for this model: %1").arg(support_.reason));
        table_->clear();
        table_->setRowCount(0);
        table_->setColumnCount(0);
        pairwise_->clear();
        pairwise_->setRowCount(0);
        pairwise_->setColumnCount(0);
        return;
    }
    {
        const QSignalBlocker b1(type_);
        const QSignalBlocker b2(period_);
        type_->clear();
        for (const SpilloverType t : {SpilloverType::Gir, SpilloverType::Oir})
        {
            type_->addItem(spilloverTypeName(t), static_cast<int>(t));
        }
        if (support_.sign)
        {
            type_->addItem(spilloverTypeName(SpilloverType::Sign), static_cast<int>(SpilloverType::Sign));
        }
        period_->setMaximum(std::max(1, support_.periods));
        period_->setValue(std::max(1, support_.periods));
        period_->setEnabled(true);
    }
    period_->setVisible(support_.moving);
    periodLabel_->setVisible(support_.moving);
    bars_->setAxisScaleDraw(QwtPlot::xBottom, new NameScaleDraw(support_.variables));
    schedule();
}

SpilloverRequest SpilloverView::request() const
{
    SpilloverRequest r;
    r.type = static_cast<SpilloverType>(type_->currentData().toInt());
    r.horizon = horizon_->value();
    r.period = support_.moving && r.type != SpilloverType::Sign ? period_->value() : -1;
    return r;
}

void SpilloverView::schedule()
{
    if (support_.ok)
    {
        debounce_->start();
    }
}

void SpilloverView::compute()
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
    const SpilloverRequest req = request();
    const bool moving = support_.moving;
    watcher_->setFuture(QtConcurrent::run([file, req, moving]() {
        const std::lock_guard<std::recursive_mutex> hdf5(hdf5Mutex());
        Result r;
        try
        {
            r.measures = spillovers(file, req);
        }
        catch (const std::exception &e)
        {
            r.error = QString::fromUtf8(e.what());
            return r;
        }
        if (moving)
        {
            try
            {
                // Every period over a thinned set of draws: the quantiles of
                // a path need fewer than one period's table does.
                SpilloverRequest each = req;
                each.maxDraws = std::min(req.maxDraws, 1000);
                r.path = spilloverPath(file, each);
            }
            catch (const std::exception &e)
            {
                r.pathError = QString::fromUtf8(e.what());
            }
        }
        return r;
    }));
}

void SpilloverView::show(const Result &result)
{
    last_ = result;
    if (!result.error.isEmpty())
    {
        status_->setText(tr("✗  %1").arg(result.error));
        return;
    }
    const Spillovers &m = result.measures;
    const int k = support_.k;
    const auto ku = static_cast<arma::uword>(k);
    const Summary total = summarise(m.total);
    const SpilloverRequest req = request();
    status_->setText(tr("Total spillover index %1% (90%: %2 to %3), %4, H = %5, over %6 draws%7.")
                         .arg(percent(total.median), percent(total.low), percent(total.high),
                              spilloverTypeName(req.type).section(QLatin1Char(' '), 0, 0).toLower())
                         .arg(req.horizon)
                         .arg(m.total.n_elem)
                         .arg(support_.moving ? tr(", at period %1").arg(m.period) : QString()));

    // The table, shaded by share; the diagonal is the own share and kept plain.
    table_->clear();
    table_->setRowCount(k + 1);
    table_->setColumnCount(k + 1);
    QStringList columns = support_.variables;
    columns << tr("From others");
    QStringList rows = support_.variables;
    rows << tr("To others");
    table_->setHorizontalHeaderLabels(columns);
    table_->setVerticalHeaderLabels(rows);
    const double offMax = [&]() {
        double v = 1e-12;
        for (arma::uword i = 0; i < ku; ++i)
            for (arma::uword j = 0; j < ku; ++j)
                if (i != j)
                    v = std::max(v, m.table(i, j));
        return v;
    }();
    for (arma::uword i = 0; i < ku; ++i)
    {
        for (arma::uword j = 0; j < ku; ++j)
        {
            const QColor shade = i == j ? QColor() : seriesColor(0, static_cast<int>(20 + 140 * m.table(i, j) / offMax));
            table_->setItem(static_cast<int>(i), static_cast<int>(j), cell(percent(m.table(i, j)), shade));
        }
        const Summary from = summarise(m.from.col(i));
        auto *f = cell(percent(arma::mean(m.from.col(i))), {}, true);
        f->setToolTip(tr("Median %1, 90% interval %2 to %3").arg(percent(from.median), percent(from.low), percent(from.high)));
        table_->setItem(static_cast<int>(i), k, f);
        const Summary to = summarise(m.to.col(i));
        auto *t = cell(percent(arma::mean(m.to.col(i))), {}, true);
        t->setToolTip(tr("Median %1, 90% interval %2 to %3").arg(percent(to.median), percent(to.low), percent(to.high)));
        table_->setItem(k, static_cast<int>(i), t);
    }
    auto *corner = cell(percent(arma::mean(m.total)), seriesColor(1, 70), true);
    corner->setToolTip(tr("The total spillover index, posterior mean. Median %1, 90% interval %2 to %3")
                           .arg(percent(total.median), percent(total.low), percent(total.high)));
    table_->setItem(k, k, corner);

    // Net pairwise.
    pairwise_->clear();
    pairwise_->setRowCount(k);
    pairwise_->setColumnCount(k);
    pairwise_->setHorizontalHeaderLabels(support_.variables);
    pairwise_->setVerticalHeaderLabels(support_.variables);
    const double pairMax = std::max(1e-12, arma::abs(m.pairwise).max());
    for (arma::uword i = 0; i < ku; ++i)
    {
        for (arma::uword j = 0; j < ku; ++j)
        {
            const double v = m.pairwise(i, j);
            QColor shade;
            if (i != j)
            {
                shade = v > 0 ? QColor(30, 123, 74) : QColor(179, 38, 30);
                shade.setAlpha(static_cast<int>(20 + 150 * std::abs(v) / pairMax));
            }
            pairwise_->setItem(static_cast<int>(i), static_cast<int>(j),
                               cell(i == j ? QStringLiteral("–") : QString::number(v, 'f', 2), shade));
        }
    }

    showDirectional();

    // The index over the sample.
    path_->detachItems(QwtPlotItem::Rtti_PlotCurve, true);
    path_->detachItems(QwtPlotItem::Rtti_PlotIntervalCurve, true);
    if (!support_.moving)
    {
        pathNote_->setText(tr("Neither the coefficients nor the covariance of this model move, so neither "
                              "does the index. A model with time-varying coefficients or stochastic "
                              "volatility estimates it in every period of the sample at once."));
    }
    else if (!result.pathError.isEmpty())
    {
        pathNote_->setText(result.pathError + QLatin1Char(' ') +
                           tr("Choose the generalised or the orthogonalised decomposition to see the path."));
    }
    else
    {
        const SpilloverPath &p = result.path;
        pathNote_->setText(tr("The total index in every period of the sample, as the model estimates the "
                              "system there: median with 68% and 90% bands over %1 draws.")
                               .arg(p.draws));
        QVector<QwtIntervalSample> b90, b68;
        QVector<QPointF> med;
        for (std::size_t t = 0; t < p.time.size(); ++t)
        {
            const auto tu = static_cast<arma::uword>(t);
            b90.append({p.time[t], QwtInterval(p.quantiles(tu, 0), p.quantiles(tu, 4))});
            b68.append({p.time[t], QwtInterval(p.quantiles(tu, 1), p.quantiles(tu, 3))});
            med.append({p.time[t], p.quantiles(tu, 2)});
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
        path_->setAxisTitle(QwtPlot::xBottom, support_.frequency > 1 ? tr("Year") : tr("Period"));
        if (!p.time.empty())
        {
            path_->setAxisScale(QwtPlot::xBottom, p.time.front(), p.time.back());
        }
    }
    path_->replot();
}

void SpilloverView::showDirectional()
{
    bars_->detachItems(QwtPlotItem::Rtti_PlotHistogram, true);
    bars_->detachItems(QwtPlotItem::Rtti_PlotIntervalCurve, true);
    bars_->detachItems(QwtPlotItem::Rtti_PlotMarker, true);
    bars_->detachItems(QwtPlotItem::Rtti_PlotUserItem, true);
    const Spillovers &m = last_.measures;
    if (!last_.error.isEmpty() || m.total.n_elem == 0)
    {
        bars_->replot();
        return;
    }
    const arma::mat &draws = measure_->currentIndex() == 0 ? m.net : (measure_->currentIndex() == 1 ? m.to : m.from);
    QVector<QPointF> mid;
    QVector<QwtIntervalSample> ci;
    double lo = 0.0, hi = 0.0;
    for (arma::uword j = 0; j < draws.n_cols; ++j)
    {
        const Summary s = summarise(draws.col(j));
        mid.append({double(j), s.median});
        ci.append({double(j), QwtInterval(s.low, s.high)});
        lo = std::min(lo, s.low);
        hi = std::max(hi, s.high);
    }
    auto *chart = new QwtPlotBarChart;
    auto *symbol = new QwtColumnSymbol(QwtColumnSymbol::Box);
    symbol->setPalette(QPalette(seriesColor(0, 170)));
    symbol->setFrameStyle(QwtColumnSymbol::NoFrame);
    chart->setSymbol(symbol);
    chart->setLayoutPolicy(QwtPlotBarChart::ScaleSamplesToAxes);
    chart->setLayoutHint(0.55);
    chart->setSamples(mid);
    chart->attach(bars_);
    auto *whiskers = new QwtPlotIntervalCurve;
    whiskers->setStyle(QwtPlotIntervalCurve::NoCurve);
    auto *bar = new QwtIntervalSymbol(QwtIntervalSymbol::Bar);
    bar->setWidth(12);
    QColor c = bars_->palette().color(QPalette::Text);
    c.setAlpha(190);
    bar->setPen(c, 1.4);
    whiskers->setSymbol(bar);
    whiskers->setSamples(ci);
    whiskers->attach(bars_);
    zeroLine(bars_);
    const double pad = 0.1 * std::max(1e-9, hi - lo);
    bars_->setAxisScale(QwtPlot::xBottom, -0.6, double(draws.n_cols) - 0.4, 1.0);
    bars_->setAxisScale(QwtPlot::yLeft, lo - (lo < 0 ? pad : 0.0), hi + pad);
    bars_->replot();
}

} // namespace studio
