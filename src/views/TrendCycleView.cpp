// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Franz X. Mohr

#include "views/TrendCycleView.h"

#include "engine/ModelFileTree.h"
#include "views/BandPlot.h"
#include "views/PeriodBox.h"
#include "views/PlotStyle.h"
#include "views/TableExport.h"

#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QScrollArea>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <qwt_plot.h>
#include <qwt_text.h>

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

QString number(double x)
{
    return std::isfinite(x) ? QString::number(x, 'f', 3) : QStringLiteral("–");
}

QTableWidgetItem *cell(const QString &text)
{
    auto *item = new QTableWidgetItem(text);
    item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    return item;
}

} // namespace

TrendCycleView::TrendCycleView(QWidget *parent) : QWidget(parent)
{
    status_ = subtle();
    variable_ = new QComboBox;
    variable_->setToolTip(tr("The series whose trend and cycle are drawn, or all of them."));
    auto *controls = new QHBoxLayout;
    controls->addWidget(new QLabel(tr("Series")));
    controls->addWidget(variable_);
    controls->addStretch(1);

    tabs_ = new QTabWidget;
    tabs_->setObjectName(QStringLiteral("trendCycleTabs"));

    figure_ = [this]() {
        FigureSpec spec;
        spec.title = tr("Trend and cycle");
        spec.subtitle = tr("Posterior median with 68% and 90% bands");
        spec.columns = 2;
        for (QwtPlot *p : plots_)
        {
            spec.cells.push_back({p, {}});
        }
        spec.columnLabels = {tr("Data and trend"), tr("Cycle")};
        spec.name = QStringLiteral("trend-cycle");
        return spec;
    };
    plotHost_ = new QWidget;
    grid_ = new QGridLayout(plotHost_);
    grid_->setSpacing(12);
    grid_->setContentsMargins(0, 0, 0, 0);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(plotHost_);
    auto *plotPage = new QWidget;
    auto *plotLayout = new QVBoxLayout(plotPage);
    plotLayout->setContentsMargins(0, 6, 0, 0);
    plotLayout->addWidget(subtle(tr("Each series with its trend, and its cycle, y − trend − irregular: the posterior "
                                    "median with 68% and 90% bands. The cycle of a GDP series is its output gap.")));
    plotLayout->addWidget(scroll, 1);
    tabs_->addTab(plotPage, tr("Trend and cycle"));

    period_ = new PeriodBox;
    period_->setToolTip(tr("The period the table shows, by its date: the last, where a forecast starts, by default."));
    table_ = new QTableWidget;
    table_->setToolTip(tr("Each series' data, trend and cycle at the period chosen: the posterior median and the "
                          "central 90% interval. Right-click to copy or save the table."));
    table_->verticalHeader()->setVisible(true);
    attachTableExport(table_);
    auto *tablePage = new QWidget;
    auto *tableLayout = new QVBoxLayout(tablePage);
    tableLayout->setContentsMargins(0, 6, 0, 0);
    auto *tableTop = new QHBoxLayout;
    tableTop->addWidget(new QLabel(tr("At")));
    tableTop->addWidget(period_);
    tableTop->addStretch(1);
    tableTop->addWidget(tableExportButton(table_));
    tableLayout->addLayout(tableTop);
    tableLayout->addWidget(table_, 1);
    tabs_->addTab(tablePage, tr("At a period"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 8, 0, 0);
    layout->addLayout(controls);
    layout->addWidget(status_);
    layout->addWidget(tabs_, 1);

    watcher_ = new QFutureWatcher<Result>(this);
    connect(watcher_, &QFutureWatcher<Result>::finished, this, [this]() { show(watcher_->result()); });
    connect(variable_, &QComboBox::currentIndexChanged, this, &TrendCycleView::draw);
    connect(period_, &PeriodBox::valueChanged, this, &TrendCycleView::fillTable);

    setModel(QString());
}

void TrendCycleView::setModel(const QString &file)
{
    file_ = file;
    support_ = file.isEmpty() ? TrendCycleSupport{} : trendCycleSupport(file);
    const bool ok = support_.ok;
    variable_->setEnabled(ok);
    tabs_->setEnabled(ok);
    result_ = Result{};
    draw();
    table_->clear();
    if (!ok)
    {
        status_->setText(file.isEmpty() ? tr("Select an unobserved components model of the project.")
                                        : tr("Not available for this model: %1").arg(support_.reason));
        return;
    }
    {
        const QSignalBlocker block(variable_);
        variable_->clear();
        variable_->addItem(tr("All series"));
        variable_->addItems(support_.series);
    }
    {
        const QSignalBlocker block(period_);
        period_->setPeriods(support_.periods, file);
    }
    status_->setText(tr("%1: %2 series, %3 periods, %4 draws%5.")
                         .arg(support_.algorithm)
                         .arg(support_.k)
                         .arg(support_.periods)
                         .arg(support_.draws)
                         .arg(support_.irregular ? tr(", with an irregular component") : QString()));
    compute();
}

void TrendCycleView::compute()
{
    if (!support_.ok || watcher_->isRunning())
    {
        return;
    }
    const QString file = file_;
    watcher_->setFuture(QtConcurrent::run([file]() {
        const std::lock_guard<std::recursive_mutex> hdf5(hdf5Mutex());
        Result r;
        r.file = file;
        try
        {
            r.series = trendCycle(file);
        }
        catch (const std::exception &e)
        {
            r.error = QString::fromUtf8(e.what());
        }
        return r;
    }));
}

void TrendCycleView::show(const Result &result)
{
    // A model selected while this one computed: compute that one instead.
    if (result.file != file_)
    {
        compute();
        return;
    }
    result_ = result;
    if (!result.error.isEmpty())
    {
        status_->setText(tr("Could not compute: %1").arg(result.error));
        return;
    }
    draw();
    fillTable();
}

void TrendCycleView::draw()
{
    for (QwtPlot *p : plots_)
    {
        grid_->removeWidget(p);
        p->hide();
        p->deleteLater();
    }
    plots_.clear();
    if (result_.series.empty())
    {
        return;
    }
    const int pick = variable_->currentIndex() - 1;
    QFont bold = font();
    bold.setBold(true);
    int row = 0;
    for (int i = 0; i < static_cast<int>(result_.series.size()); ++i)
    {
        if (pick >= 0 && i != pick)
        {
            continue;
        }
        const TrendCycleSeries &s = result_.series[static_cast<std::size_t>(i)];
        const QString name = support_.series.value(i);

        auto *trend = new QwtPlot;
        attachFigureExport(trend, figure_);
        trend->setToolTip(tr("%1 and its trend: the posterior median with 68% and 90% bands. Right-click to copy or "
                             "save the figure.")
                              .arg(name));
        trend->setMinimumHeight(220);
        styleAxes(trend, QString(), QString());
        QwtText title(tr("%1 and its trend").arg(name));
        title.setFont(bold);
        trend->setTitle(title);
        drawPathBands(trend, support_.time, s.trend, 0);
        drawPathLine(trend, support_.time, s.data, 1, name);
        trend->replot();

        auto *cycle = new QwtPlot;
        attachFigureExport(cycle, figure_);
        cycle->setToolTip(tr("The cycle of %1: the posterior median with 68% and 90% bands. Right-click to copy or "
                             "save the figure.")
                              .arg(name));
        cycle->setMinimumHeight(220);
        styleAxes(cycle, QString(), QString());
        QwtText cycleTitle(tr("Cycle of %1").arg(name));
        cycleTitle.setFont(bold);
        cycle->setTitle(cycleTitle);
        drawPathBands(cycle, support_.time, s.cycle, 0, true);
        cycle->replot();

        grid_->addWidget(trend, row, 0);
        grid_->addWidget(cycle, row, 1);
        plots_.push_back(trend);
        plots_.push_back(cycle);
        ++row;
    }
}

void TrendCycleView::fillTable()
{
    table_->clear();
    if (result_.series.empty())
    {
        return;
    }
    const int t = std::clamp(period_->value() - 1, 0, support_.periods - 1);
    const auto at = static_cast<std::size_t>(t);
    const int k = static_cast<int>(result_.series.size());
    table_->setRowCount(k);
    table_->setColumnCount(3);
    table_->setHorizontalHeaderLabels({tr("Data"), tr("Trend"), tr("Cycle")});
    table_->setVerticalHeaderLabels(support_.series);
    for (int i = 0; i < k; ++i)
    {
        const TrendCycleSeries &s = result_.series[static_cast<std::size_t>(i)];
        table_->setItem(i, 0, cell(number(s.data[at])));
        table_->setItem(i, 1, cell(QStringLiteral("%1 [%2, %3]").arg(number(s.trend.median[at]), number(s.trend.lo90[at]),
                                                                    number(s.trend.hi90[at]))));
        table_->setItem(i, 2, cell(QStringLiteral("%1 [%2, %3]").arg(number(s.cycle.median[at]), number(s.cycle.lo90[at]),
                                                                    number(s.cycle.hi90[at]))));
    }
    table_->resizeColumnsToContents();
    setTableExportInfo(table_, tr("Trend and cycle in %1").arg(period_->currentText()),
                       tr("Posterior median [5%, 95%] of %1 draws.").arg(support_.draws));
}

} // namespace studio
