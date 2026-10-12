// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Franz X. Mohr

#include "views/HistoricalDecompositionView.h"

#include "engine/ModelFileTree.h"
#include "views/FigureExport.h"
#include "views/PlotStyle.h"

#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPen>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <qwt_column_symbol.h>
#include <qwt_legend.h>
#include <qwt_plot.h>
#include <qwt_plot_curve.h>
#include <qwt_plot_intervalcurve.h>
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

HistoricalDecompositionView::HistoricalDecompositionView(QWidget *parent) : QWidget(parent)
{
    status_ = subtle();
    response_ = new QComboBox;
    response_->setObjectName(QStringLiteral("hdResponse"));
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
        if (matrix_)
        {
            // As on screen: a column per shock and the baseline, a row per variable.
            FigureSpec spec;
            spec.title = tr("Historical decomposition");
            spec.subtitle = tr("%1, %2 contributions and their 90% bands")
                                .arg(type_->currentText(), statistic_->currentText().toLower());
            spec.columns = columns_;
            for (QwtPlot *p : plots_)
            {
                spec.cells.push_back({p, {}});
            }
            for (QWidget *h : headers_)
            {
                if (auto *l = qobject_cast<QLabel *>(h); l && spec.columnLabels.size() < columns_)
                {
                    spec.columnLabels << l->text();
                }
            }
            spec.rowLabels = support_.variables;
            spec.name = QStringLiteral("hd-all");
            return spec;
        }
        FigureSpec spec = singleFigure(plot_, tr("Historical decomposition of %1").arg(response_->currentText()),
                                       QStringLiteral("hd-%1").arg(response_->currentText()));
        spec.subtitle = tr("%1, %2 contributions").arg(type_->currentText(), statistic_->currentText().toLower());
        return spec;
    };
    figure_ = figure;
    controls->addWidget(figureExportButton(figure));
    attachFigureExport(plot_, figure);
    styleAxes(plot_, tr("Period"), QString());
    plot_->insertLegend(new QwtLegend, QwtPlot::RightLegend);

    // Every variable: the matrix, in a scroll area of its own.
    auto *gridHost = new QWidget;
    grid_ = new QGridLayout(gridHost);
    grid_->setSpacing(12);
    scroll_ = new QScrollArea;
    scroll_->setWidget(gridHost);
    scroll_->setWidgetResizable(true);
    scroll_->setFrameShape(QFrame::NoFrame);
    scroll_->hide();

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 8, 0, 0);
    layout->addLayout(controls);
    layout->addWidget(status_);
    layout->addWidget(plot_, 1);
    layout->addWidget(scroll_, 1);

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
    // The matrix gains or loses its baseline column, and the layout changes
    // with all variables or one.
    connect(baseline_, &QCheckBox::toggled, this, [this]() {
        rebuildGrid();
        show();
    });
    connect(response_, &QComboBox::currentIndexChanged, this, [this]() {
        if (support_.ok && allVariables() != matrix_)
        {
            rebuildGrid();
        }
    });
    // Sign-identified shocks are named for their column: the headers change.
    connect(type_, &QComboBox::currentIndexChanged, this, [this]() {
        if (matrix_)
        {
            rebuildGrid();
        }
    });
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
        rebuildGrid();
        show();
        return;
    }
    {
        const QSignalBlocker b1(response_);
        const QSignalBlocker b2(type_);
        response_->clear();
        response_->addItem(tr("All variables"));
        response_->addItems(support_.variables);
        // The matrix while it stays legible, as the structural analysis.
        response_->setCurrentIndex(support_.k <= 6 ? 0 : 1);
        type_->clear();
        type_->addItem(tr("Orthogonalised (Choleski)"), static_cast<int>(HdType::Oir));
        if (support_.sign)
        {
            type_->addItem(tr("Sign restrictions"), static_cast<int>(HdType::Sign));
            type_->setCurrentIndex(1);
        }
    }
    rebuildGrid();
    schedule();
}

bool HistoricalDecompositionView::allVariables() const
{
    return response_->currentIndex() == 0;
}

void HistoricalDecompositionView::rebuildGrid()
{
    // Out of the grid at once, so no old panel stands beside the new ones.
    for (QWidget *w : std::vector<QWidget *>(plots_.begin(), plots_.end()))
    {
        grid_->removeWidget(w);
        w->hide();
        w->deleteLater();
    }
    plots_.clear();
    for (QWidget *h : headers_)
    {
        grid_->removeWidget(h);
        h->hide();
        h->deleteLater();
    }
    headers_.clear();
    const int k = support_.ok ? support_.k : 0;
    matrix_ = k > 0 && allVariables();
    plot_->setVisible(!matrix_);
    scroll_->setVisible(matrix_);
    if (!matrix_)
    {
        columns_ = 0;
        return;
    }
    const bool sign = static_cast<HdType>(type_->currentData().toInt()) == HdType::Sign;
    columns_ = baseline_->isChecked() ? k + 1 : k;
    QFont bold = font();
    bold.setBold(true);
    for (int j = 0; j < columns_; ++j)
    {
        const QString name = support_.variables.value(j);
        auto *head = new QLabel(j == k ? tr("Baseline") : sign ? tr("%1 shock").arg(name) : tr("Shock to %1").arg(name));
        head->setAlignment(Qt::AlignCenter);
        head->setFont(bold);
        head->setWordWrap(true);
        grid_->addWidget(head, 0, j + 1);
        headers_.push_back(head);
    }
    for (int i = 0; i < k; ++i)
    {
        auto *row = new QLabel(support_.variables.value(i));
        row->setAlignment(Qt::AlignCenter);
        row->setFont(bold);
        row->setMinimumWidth(70);
        row->setToolTip(tr("The path of %1 over the sample, split into what each shock along the row did to it.")
                            .arg(support_.variables.value(i)));
        grid_->addWidget(row, i + 1, 0);
        headers_.push_back(row);
        for (int j = 0; j < columns_; ++j)
        {
            auto *plot = new QwtPlot;
            attachFigureExport(plot, figure_);
            plot->setToolTip(j == k ? tr("The baseline of %1 -- what the shocks of the sample leave of it: the deterministic terms, the exogenous variables and the initial conditions -- with its 90% band. Right-click to copy or save the figure.")
                                          .arg(support_.variables.value(i))
                                    : tr("The contribution of the shock to %2 to %1 over the sample, with its 90% band. Right-click to copy or save the figure.")
                                          .arg(support_.variables.value(i), support_.variables.value(j)));
            plot->setMinimumHeight(170);
            styleAxes(plot, i + 1 == k ? (support_.frequency > 1 ? tr("Year") : tr("Period")) : QString(), QString());
            grid_->addWidget(plot, i + 1, j + 1);
            plots_.push_back(plot);
        }
    }
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
    req.median = statistic_->currentData().toBool();
    std::vector<int> variables;
    if (allVariables())
    {
        for (int i = 0; i < support_.k; ++i)
        {
            variables.push_back(i);
        }
    }
    else
    {
        variables.push_back(response_->currentIndex() - 1);
    }
    watcher_->setFuture(QtConcurrent::run([file, req, variables]() {
        Result r;
        try
        {
            r.variables = variables;
            for (const int i : variables)
            {
                HdRequest one = req;
                one.response = i;
                r.hd.push_back(historicalDecomposition(file, one));
            }
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
    // A result for the other layout is stale; the next one is coming.
    if (last_.hd.empty() || (last_.hd.size() > 1) != (matrix_ && support_.k > 1))
    {
        plot_->replot();
        return;
    }
    const bool sign = static_cast<HdType>(type_->currentData().toInt()) == HdType::Sign;
    const bool withBaseline = baseline_->isChecked();
    const auto k = static_cast<arma::uword>(support_.k);
    const double width = 1.0 / support_.frequency;
    if (matrix_)
    {
        status_->setText(tr("The contribution of each %1 shock (columns) to each variable (rows) over the sample%2: "
                            "%3 and 90% bands over %4 draws.")
                             .arg(sign ? tr("sign-identified") : tr("orthogonalised"),
                                  withBaseline ? tr(", and the baseline") : QString(),
                                  statistic_->currentText().toLower())
                             .arg(last_.hd.front().draws));
        const auto columns = static_cast<std::size_t>(std::max(1, columns_));
        for (std::size_t p = 0; p < plots_.size(); ++p)
        {
            const std::size_t i = p / columns, j = p % columns;
            if (i >= last_.hd.size() || j >= last_.hd[i].centre.n_cols)
            {
                continue;
            }
            const HistoricalDecomposition &hd = last_.hd[i];
            QwtPlot *plot = plots_[p];
            plot->detachItems(QwtPlotItem::Rtti_PlotCurve, true);
            plot->detachItems(QwtPlotItem::Rtti_PlotIntervalCurve, true);
            plot->detachItems(QwtPlotItem::Rtti_PlotMarker, true);
            zeroLine(plot);
            QVector<QwtIntervalSample> band;
            QVector<QPointF> centre;
            for (arma::uword t = 0; t < hd.centre.n_rows; ++t)
            {
                band << QwtIntervalSample(hd.time[t], hd.lower(t, j), hd.upper(t, j));
                centre << QPointF(hd.time[t], hd.centre(t, j));
            }
            auto *tube = new QwtPlotIntervalCurve(tr("90%"));
            tube->setStyle(QwtPlotIntervalCurve::Tube);
            tube->setPen(QPen(Qt::transparent, 0.0));
            tube->setBrush(seriesColor(0, 70));
            tube->setRenderHint(QwtPlotItem::RenderAntialiased);
            tube->setSamples(band);
            tube->attach(plot);
            auto *line = new QwtPlotCurve(statistic_->currentText());
            line->setPen(seriesColor(0), 1.6);
            line->setRenderHint(QwtPlotItem::RenderAntialiased);
            line->setSamples(centre);
            line->attach(plot);
            plot->setAxisScale(QwtPlot::xBottom, hd.time.front() - width, hd.time.back() + width);
            plot->setAxisAutoScale(QwtPlot::yLeft);
            plot->replot();
        }
        return;
    }
    const HistoricalDecomposition &hd = last_.hd.front();
    if (hd.centre.n_rows == 0)
    {
        plot_->replot();
        return;
    }
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
    stack(titles, positive, width, true)->attach(plot_);
    stack(titles, negative, width, false)->attach(plot_);
    auto *sumLine = new QwtPlotCurve(withBaseline ? tr("Data") : tr("Sum of the shocks"));
    sumLine->setPen(plot_->palette().color(QPalette::Text), 2.0);
    sumLine->setRenderHint(QwtPlotItem::RenderAntialiased);
    sumLine->setSamples(line);
    sumLine->attach(plot_);
    zeroLine(plot_);
    plot_->setAxisTitle(QwtPlot::xBottom, support_.frequency > 1 ? tr("Year") : tr("Period"));
    plot_->setAxisScale(QwtPlot::xBottom, hd.time.front() - width, hd.time.back() + width);
    plot_->setAxisAutoScale(QwtPlot::yLeft);
    plot_->replot();
}

} // namespace studio
