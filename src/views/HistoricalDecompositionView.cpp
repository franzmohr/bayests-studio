#include "views/HistoricalDecompositionView.h"

#include "engine/ModelFileTree.h"
#include "views/FigureExport.h"
#include "views/PlotStyle.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPen>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <qwt_column_symbol.h>
#include <qwt_legend.h>
#include <qwt_plot.h>
#include <qwt_plot_curve.h>
#include <qwt_plot_marker.h>
#include <qwt_plot_multi_barchart.h>
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

/// One stacked chart of the parts of one sign; every value has that sign, so
/// the stack grows away from zero.
QwtPlotMultiBarChart *stack(const QList<QwtText> &titles, const QVector<QwtSetSample> &samples, double width,
                            bool legend)
{
    auto *chart = new QwtPlotMultiBarChart;
    chart->setStyle(QwtPlotMultiBarChart::Stacked);
    chart->setLayoutPolicy(QwtPlotMultiBarChart::ScaleSamplesToAxes);
    chart->setLayoutHint(width);
    chart->setSpacing(0);
    chart->setBarTitles(titles);
    for (int j = 0; j < titles.size(); ++j)
    {
        auto *symbol = new QwtColumnSymbol(QwtColumnSymbol::Box);
        symbol->setPalette(QPalette(seriesColor(j, 210, static_cast<int>(titles.size()))));
        symbol->setFrameStyle(QwtColumnSymbol::NoFrame);
        chart->setSymbol(j, symbol);
    }
    chart->setSamples(samples);
    chart->setItemAttribute(QwtPlotItem::Legend, legend);
    return chart;
}

} // namespace

HistoricalDecompositionView::HistoricalDecompositionView(QWidget *parent) : QWidget(parent)
{
    status_ = subtle();
    response_ = new QComboBox;
    response_->setMinimumContentsLength(10);
    response_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    response_->setToolTip(tr("The variable whose path over the sample is decomposed."));
    type_ = new QComboBox;
    type_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    type_->setToolTip(tr("How the shocks are identified: orthogonalised (Cholesky, in the model's variable order) or the sign restrictions set for the model."));
    statistic_ = new QComboBox;
    statistic_->addItem(tr("Posterior mean"), false);
    statistic_->addItem(tr("Posterior median"), true);
    statistic_->setToolTip(tr("Which posterior summary of each shock's contribution the bars show. The means add up to the data; the medians need not."));
    baseline_ = new QCheckBox(tr("Show the baseline"));
    baseline_->setToolTip(tr("What the shocks of the sample leave of the data: the deterministic terms, the "
                             "exogenous variables and the initial conditions."));

    auto *controls = new QHBoxLayout;
    controls->addWidget(new QLabel(tr("Variable")));
    controls->addWidget(response_);
    controls->addSpacing(12);
    controls->addWidget(new QLabel(tr("Shocks")));
    controls->addWidget(type_);
    controls->addSpacing(12);
    controls->addWidget(statistic_);
    controls->addSpacing(12);
    controls->addWidget(baseline_);
    controls->addStretch(1);

    plot_ = new QwtPlot;
    plot_->setObjectName(QStringLiteral("hdPlot"));
    plot_->setToolTip(tr("Each shock's contribution to the variable, period by period, against what the shocks add up to. Right-click to copy or save the figure."));
    const FigureProvider figure = [this]() {
        FigureSpec spec = singleFigure(plot_, tr("Historical decomposition of %1").arg(response_->currentText()),
                                       QStringLiteral("hd-%1").arg(response_->currentText()));
        spec.subtitle = tr("%1, %2 contributions").arg(type_->currentText(), statistic_->currentText().toLower());
        return spec;
    };
    controls->addWidget(figureExportButton(figure));
    attachFigureExport(plot_, figure);
    styleAxes(plot_, tr("Period"), QString());
    plot_->insertLegend(new QwtLegend, QwtPlot::RightLegend);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 8, 0, 0);
    layout->addLayout(controls);
    layout->addWidget(status_);
    layout->addWidget(plot_, 1);

    watcher_ = new QFutureWatcher<Result>(this);
    connect(watcher_, &QFutureWatcher<Result>::finished, this, [this]() {
        last_ = watcher_->result();
        show();
        if (pending_)
        {
            pending_ = false;
            compute();
        }
    });
    debounce_ = new QTimer(this);
    debounce_->setSingleShot(true);
    debounce_->setInterval(200);
    connect(debounce_, &QTimer::timeout, this, &HistoricalDecompositionView::compute);
    for (QComboBox *c : {response_, type_, statistic_})
    {
        connect(c, &QComboBox::currentIndexChanged, this, &HistoricalDecompositionView::schedule);
    }
    connect(baseline_, &QCheckBox::toggled, this, &HistoricalDecompositionView::show);
    setModel(QString());
}

void HistoricalDecompositionView::setModel(const QString &file)
{
    file_ = file;
    support_ = file.isEmpty() ? HdSupport{} : hdSupport(file);
    const bool ok = support_.ok;
    for (QWidget *w : std::initializer_list<QWidget *>{response_, type_, statistic_, baseline_})
    {
        w->setEnabled(ok);
    }
    last_ = Result{};
    if (!ok)
    {
        status_->setText(file.isEmpty() ? tr("Select a model of the project.")
                                        : tr("Not available for this model: %1").arg(support_.reason));
        show();
        return;
    }
    {
        const QSignalBlocker b1(response_);
        const QSignalBlocker b2(type_);
        response_->clear();
        response_->addItems(support_.variables);
        type_->clear();
        type_->addItem(tr("Orthogonalised (Choleski)"), static_cast<int>(HdType::Oir));
        if (support_.sign)
        {
            type_->addItem(tr("Sign restrictions"), static_cast<int>(HdType::Sign));
            type_->setCurrentIndex(1);
        }
    }
    schedule();
}

void HistoricalDecompositionView::schedule()
{
    if (support_.ok)
    {
        debounce_->start();
    }
}

void HistoricalDecompositionView::compute()
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
    HdRequest req;
    req.type = static_cast<HdType>(type_->currentData().toInt());
    req.response = response_->currentIndex();
    req.median = statistic_->currentData().toBool();
    watcher_->setFuture(QtConcurrent::run([file, req]() {
        Result r;
        try
        {
            r.hd = historicalDecomposition(file, req);
        }
        catch (const std::exception &e)
        {
            r.error = QString::fromUtf8(e.what());
        }
        return r;
    }));
}

void HistoricalDecompositionView::show()
{
    plot_->detachItems(QwtPlotItem::Rtti_PlotMultiBarChart, true);
    plot_->detachItems(QwtPlotItem::Rtti_PlotCurve, true);
    plot_->detachItems(QwtPlotItem::Rtti_PlotMarker, true);
    if (!last_.error.isEmpty())
    {
        status_->setText(tr("✗  %1").arg(last_.error));
        plot_->replot();
        return;
    }
    const HistoricalDecomposition &hd = last_.hd;
    if (hd.centre.n_rows == 0)
    {
        plot_->replot();
        return;
    }
    const bool sign = static_cast<HdType>(type_->currentData().toInt()) == HdType::Sign;
    const bool withBaseline = baseline_->isChecked();
    const auto k = static_cast<arma::uword>(support_.k);
    const arma::uword parts = withBaseline ? k + 1 : k;
    status_->setText(tr("The contribution of each %1 shock to %2 over the sample%3, %4 over %5 draws; the line is "
                        "their sum%6.")
                         .arg(sign ? tr("sign-identified") : tr("orthogonalised"), response_->currentText())
                         .arg(withBaseline ? tr(" and the baseline") : QString())
                         .arg(statistic_->currentText().toLower())
                         .arg(hd.draws)
                         .arg(withBaseline ? tr(", the data") : QString()));

    QList<QwtText> titles;
    for (arma::uword j = 0; j < k; ++j)
    {
        titles << QwtText(sign ? tr("%1 shock").arg(support_.variables.value(static_cast<int>(j)))
                               : support_.variables.value(static_cast<int>(j)));
    }
    if (withBaseline)
    {
        titles << QwtText(tr("Baseline"));
    }
    QVector<QwtSetSample> positive, negative;
    QVector<QPointF> line;
    for (arma::uword t = 0; t < hd.centre.n_rows; ++t)
    {
        QVector<double> up, down;
        double sum = 0.0;
        for (arma::uword j = 0; j < parts; ++j)
        {
            const double v = hd.centre(t, j);
            up << std::max(v, 0.0);
            down << std::min(v, 0.0);
            sum += v;
        }
        positive << QwtSetSample(hd.time[t], up);
        negative << QwtSetSample(hd.time[t], down);
        line << QPointF(hd.time[t], sum);
    }
    const double width = 1.0 / support_.frequency;
    stack(titles, positive, width, true)->attach(plot_);
    stack(titles, negative, width, false)->attach(plot_);
    auto *sumLine = new QwtPlotCurve(withBaseline ? tr("Data") : tr("Sum of the shocks"));
    sumLine->setPen(plot_->palette().color(QPalette::Text), 2.0);
    sumLine->setRenderHint(QwtPlotItem::RenderAntialiased);
    sumLine->setSamples(line);
    sumLine->attach(plot_);
    auto *zero = new QwtPlotMarker;
    zero->setLineStyle(QwtPlotMarker::HLine);
    zero->setYValue(0.0);
    QColor c = plot_->palette().color(QPalette::Text);
    c.setAlpha(110);
    zero->setLinePen(c, 1.0);
    zero->attach(plot_);
    plot_->setAxisTitle(QwtPlot::xBottom, support_.frequency > 1 ? tr("Year") : tr("Period"));
    plot_->setAxisScale(QwtPlot::xBottom, hd.time.front() - width, hd.time.back() + width);
    plot_->setAxisAutoScale(QwtPlot::yLeft);
    plot_->replot();
}

} // namespace studio
