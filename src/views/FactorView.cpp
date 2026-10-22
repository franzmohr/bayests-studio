// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Franz X. Mohr

#include "views/FactorView.h"

#include "engine/ModelFileTree.h"
#include "views/BandPlot.h"
#include "views/PeriodBox.h"
#include "views/PlotStyle.h"
#include "views/SeriesPlot.h"
#include "views/TableExport.h"

#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <qwt_plot.h>
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

QString number(double x)
{
    return std::isfinite(x) ? QString::number(x, 'f', 3) : QStringLiteral("–");
}

/// A scrolling host for a grid of plots.
QScrollArea *gridScroll(QWidget *&host, QGridLayout *&grid)
{
    host = new QWidget;
    grid = new QGridLayout(host);
    grid->setSpacing(12);
    grid->setContentsMargins(0, 0, 0, 0);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(host);
    return scroll;
}

void clearGrid(QGridLayout *grid, std::vector<QwtPlot *> &plots)
{
    for (QwtPlot *p : plots)
    {
        grid->removeWidget(p);
        p->hide();
        p->deleteLater();
    }
    plots.clear();
}

QwtPlot *titledPlot(const QString &title, const QString &xTitle, const FigureProvider &figure, const QString &tip)
{
    auto *plot = new QwtPlot;
    attachFigureExport(plot, figure);
    plot->setToolTip(tip);
    plot->setMinimumHeight(220);
    styleAxes(plot, xTitle, QString());
    QwtText text(title);
    QFont bold = plot->font();
    bold.setBold(true);
    text.setFont(bold);
    plot->setTitle(text);
    return plot;
}

QTableWidgetItem *cell(const QString &text)
{
    auto *item = new QTableWidgetItem(text);
    item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    return item;
}

} // namespace

FactorView::FactorView(QWidget *parent) : QWidget(parent)
{
    status_ = subtle();

    // The period a moving model is read at, the shock and the horizon of the
    // responses: one row above every tab.
    period_ = new PeriodBox;
    period_->setToolTip(tr("For a model whose loadings, transition or factor variances move: the period whose "
                           "loadings and responses are shown, by its date."));
    periodLabel_ = new QLabel(tr("At"));
    shock_ = new QComboBox;
    shock_->setToolTip(tr("The shock to the state equation the responses are to. A DFM's factor innovations are "
                          "independent; a FAVAR's are the Choleski factor of their covariance with the observed "
                          "factors ordered last, as Bernanke, Boivin and Eliasz (2005)."));
    horizon_ = new QSpinBox;
    horizon_->setRange(1, 120);
    horizon_->setValue(20);
    horizon_->setToolTip(tr("The last horizon of the impulse responses."));
    auto *controls = new QHBoxLayout;
    controls->addWidget(periodLabel_);
    controls->addWidget(period_);
    controls->addSpacing(12);
    controls->addWidget(new QLabel(tr("Shock")));
    controls->addWidget(shock_);
    controls->addSpacing(12);
    controls->addWidget(new QLabel(tr("Horizon")));
    controls->addWidget(horizon_);
    controls->addStretch(1);

    tabs_ = new QTabWidget;
    tabs_->setObjectName(QStringLiteral("factorTabs"));

    // Factors.
    factorFigure_ = [this]() {
        FigureSpec spec;
        spec.title = tr("Factors");
        spec.subtitle = tr("Posterior median with 68% and 90% bands");
        spec.columns = support_.factors > 1 ? 2 : 1;
        for (QwtPlot *p : factorPlots_)
        {
            spec.cells.push_back({p, {}});
        }
        spec.name = QStringLiteral("factors");
        return spec;
    };
    auto *factorPage = new QWidget;
    auto *factorLayout = new QVBoxLayout(factorPage);
    factorLayout->setContentsMargins(0, 6, 0, 0);
    factorLayout->addWidget(subtle(tr("Each unobserved factor's path over the sample: the posterior median with "
                                      "68% and 90% bands. A DFM's factors are identified by a unit lower triangular "
                                      "block of loadings, a FAVAR's by an identity block.")));
    factorLayout->addWidget(gridScroll(factorHost_, factorGrid_), 1);
    tabs_->addTab(factorPage, tr("Factors"));

    // Loadings.
    loadings_ = new QTableWidget;
    loadings_->setToolTip(tr("The loadings of each series on the state: the posterior median and the central 90% "
                             "interval. Right-click to copy or save the table."));
    loadings_->verticalHeader()->setVisible(true);
    attachTableExport(loadings_);
    loadingsNote_ = subtle();
    auto *loadingsPage = new QWidget;
    auto *loadingsLayout = new QVBoxLayout(loadingsPage);
    loadingsLayout->setContentsMargins(0, 6, 0, 0);
    auto *loadingsTop = new QHBoxLayout;
    loadingsTop->addWidget(loadingsNote_, 1);
    loadingsTop->addWidget(tableExportButton(loadings_));
    loadingsLayout->addLayout(loadingsTop);
    loadingsLayout->addWidget(loadings_, 1);
    tabs_->addTab(loadingsPage, tr("Loadings"));

    // Variance explained.
    share_ = new QTableWidget;
    share_->setToolTip(tr("The share of each series' variance over the sample its common component accounts for, "
                          "R² = 1 − var(x − C d − Λ s) / var(x − C d), per draw. Right-click to copy or save the table."));
    share_->verticalHeader()->setVisible(true);
    attachTableExport(share_);
    auto *sharePage = new QWidget;
    auto *shareLayout = new QVBoxLayout(sharePage);
    shareLayout->setContentsMargins(0, 6, 0, 0);
    auto *shareTop = new QHBoxLayout;
    shareTop->addWidget(subtle(tr("How much of each series the factors explain: R² of its common component over "
                                  "the sample, the posterior median and the central 90% interval.")),
                        1);
    shareTop->addWidget(tableExportButton(share_));
    shareLayout->addLayout(shareTop);
    shareLayout->addWidget(share_, 1);
    tabs_->addTab(sharePage, tr("Variance explained"));

    // Impulse responses: a list of the series to show beside their panels.
    responseFigure_ = [this]() {
        FigureSpec spec;
        spec.title = tr("Responses to a shock to %1").arg(shock_->currentText());
        spec.subtitle = tr("Posterior median with 68% and 90% bands");
        spec.columns = std::min<int>(3, std::max<int>(1, static_cast<int>(responsePlots_.size())));
        for (QwtPlot *p : responsePlots_)
        {
            spec.cells.push_back({p, {}});
        }
        spec.name = QStringLiteral("factor-responses");
        return spec;
    };
    responseList_ = new QListWidget;
    responseList_->setToolTip(tr("The series whose responses are drawn. A FAVAR's observed factors are at the end."));
    responseList_->setMaximumWidth(220);
    responsesNote_ = subtle();
    auto *responsePage = new QWidget;
    auto *responseLayout = new QVBoxLayout(responsePage);
    responseLayout->setContentsMargins(0, 6, 0, 0);
    responseLayout->addWidget(responsesNote_);
    auto *responseSplit = new QHBoxLayout;
    responseSplit->addWidget(responseList_);
    responseSplit->addWidget(gridScroll(responseHost_, responseGrid_), 1);
    responseLayout->addLayout(responseSplit, 1);
    tabs_->addTab(responsePage, tr("Impulse responses"));

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
    connect(debounce_, &QTimer::timeout, this, &FactorView::compute);
    connect(shock_, &QComboBox::currentIndexChanged, this, &FactorView::schedule);
    connect(horizon_, &QSpinBox::valueChanged, this, &FactorView::schedule);
    connect(period_, &PeriodBox::valueChanged, this, &FactorView::schedule);
    connect(responseList_, &QListWidget::itemChanged, this, &FactorView::drawResponses);

    setModel(QString());
}

void FactorView::setModel(const QString &file)
{
    file_ = file;
    support_ = file.isEmpty() ? FactorSupport{} : factorSupport(file);
    const bool ok = support_.ok;
    for (QWidget *w : std::initializer_list<QWidget *>{shock_, horizon_, period_, tabs_})
    {
        w->setEnabled(ok);
    }
    result_ = Result{};
    clearGrid(factorGrid_, factorPlots_);
    clearGrid(responseGrid_, responsePlots_);
    loadings_->clear();
    share_->clear();
    responseList_->clear();
    if (!ok)
    {
        status_->setText(file.isEmpty() ? tr("Select a factor model of the project.")
                                        : tr("Not available for this model: %1").arg(support_.reason));
        return;
    }
    const bool moving = support_.timeVarying || support_.stochvol;
    period_->setPeriods(support_.periods, file);
    period_->setVisible(moving);
    periodLabel_->setVisible(moving);

    {
        const QSignalBlocker block(shock_);
        shock_->clear();
        shock_->addItems(support_.state);
    }
    {
        const QSignalBlocker block(responseList_);
        const QStringList names = responseNames();
        for (int i = 0; i < names.size(); ++i)
        {
            auto *item = new QListWidgetItem(names[i], responseList_);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            // The first few series, and every observed factor.
            item->setCheckState(i < std::min(6, support_.k) || i >= support_.k ? Qt::Checked : Qt::Unchecked);
        }
    }

    // A panel per factor.
    const int columns = support_.factors > 1 ? 2 : 1;
    for (int j = 0; j < support_.factors; ++j)
    {
        QwtPlot *plot = titledPlot(support_.state.value(j), QString(), factorFigure_,
                                   tr("The factor's path: the posterior median with 68% and 90% bands. Right-click to "
                                      "copy or save the figure."));
        factorGrid_->addWidget(plot, j / columns, j % columns);
        factorPlots_.push_back(plot);
    }
    status_->setText(tr("%1: %2 series on %3 factor(s)%4, %5 periods, %6 draws.")
                         .arg(support_.algorithm)
                         .arg(support_.k)
                         .arg(support_.factors)
                         .arg(support_.favar ? tr(" and %1 observed").arg(support_.observedFactors) : QString())
                         .arg(support_.periods)
                         .arg(support_.draws));
    schedule();
}

QStringList FactorView::responseNames() const
{
    return support_.series + support_.observed;
}

void FactorView::schedule()
{
    if (support_.ok)
    {
        debounce_->start();
    }
}

void FactorView::compute()
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
    const QString file = file_;
    const bool moving = support_.timeVarying || support_.stochvol;
    const int period = moving ? period_->value() - 1 : -1;
    const int shock = std::max(0, shock_->currentIndex());
    const int horizon = horizon_->value();
    const bool responses = support_.responses;
    watcher_->setFuture(QtConcurrent::run([file, period, shock, horizon, responses]() {
        const std::lock_guard<std::recursive_mutex> hdf5(hdf5Mutex());
        Result r;
        r.file = file;
        try
        {
            r.paths = factorPaths(file);
            r.loadings = loadingTable(file, period);
            r.share = explainedShare(file);
        }
        catch (const std::exception &e)
        {
            r.error = QString::fromUtf8(e.what());
            return r;
        }
        if (responses)
        {
            try
            {
                r.responses = factorResponses(file, shock, horizon, period);
            }
            catch (const std::exception &e)
            {
                r.responsesError = QString::fromUtf8(e.what());
            }
        }
        return r;
    }));
}

void FactorView::show(const Result &result)
{
    // Computed for a model since replaced: the next round is for this one.
    if (result.file != file_)
    {
        return;
    }
    result_ = result;
    if (!result.error.isEmpty())
    {
        status_->setText(tr("Could not compute: %1").arg(result.error));
        return;
    }
    drawFactors();
    fillTables();
    drawResponses();
}

void FactorView::drawFactors()
{
    for (std::size_t j = 0; j < factorPlots_.size() && j < result_.paths.size(); ++j)
    {
        QwtPlot *plot = factorPlots_[j];
        clearBandPlot(plot);
        drawPathBands(plot, support_.time, result_.paths[j], static_cast<int>(j), true);
        plot->replot();
    }
}

void FactorView::fillTables()
{
    const int k = support_.k;
    const int ns = static_cast<int>(support_.state.size());
    const LoadingTable &l = result_.loadings;
    loadings_->setRowCount(k);
    loadings_->setColumnCount(ns);
    loadings_->setHorizontalHeaderLabels(support_.state);
    loadings_->setVerticalHeaderLabels(support_.series);
    for (int i = 0; i < k && l.median.n_rows == static_cast<arma::uword>(k); ++i)
    {
        for (int j = 0; j < ns; ++j)
        {
            loadings_->setItem(i, j, cell(QStringLiteral("%1 [%2, %3]")
                                              .arg(number(l.median(i, j)), number(l.lower(i, j)), number(l.upper(i, j)))));
        }
    }
    loadings_->resizeColumnsToContents();
    const bool moving = support_.timeVarying;
    const QString at = moving ? tr(" in %1").arg(period_->currentText()) : QString();
    loadingsNote_->setText(tr("Each series' loadings%1: the posterior median and, in brackets, the central 90% "
                              "interval.%2")
                               .arg(at, support_.favar ? tr(" The leading block is fixed at the identity, which "
                                                             "identifies the factors.")
                                                       : tr(" The leading block is fixed: ones on the diagonal, "
                                                            "zeros above it.")));
    setTableExportInfo(loadings_, tr("Factor loadings%1").arg(at),
                       tr("Posterior median [5%, 95%] of %1 draws.").arg(support_.draws));

    const ExplainedShare &s = result_.share;
    share_->setRowCount(k);
    share_->setColumnCount(2);
    share_->setHorizontalHeaderLabels({tr("R² (median)"), tr("90% interval")});
    share_->setVerticalHeaderLabels(support_.series);
    for (int i = 0; i < k && static_cast<int>(s.median.size()) == k; ++i)
    {
        share_->setItem(i, 0, cell(number(s.median[static_cast<std::size_t>(i)])));
        share_->setItem(i, 1, cell(QStringLiteral("[%1, %2]").arg(number(s.lower[static_cast<std::size_t>(i)]),
                                                                  number(s.upper[static_cast<std::size_t>(i)]))));
    }
    share_->resizeColumnsToContents();
    setTableExportInfo(share_, tr("Variance explained by the factors"),
                       tr("R² of each series' common component over the sample: posterior median and central 90% "
                          "interval."));
}

void FactorView::drawResponses()
{
    clearGrid(responseGrid_, responsePlots_);
    responseColumns_.clear();
    if (!support_.responses)
    {
        responsesNote_->setText(tr("Not available for this model: %1").arg(support_.responsesReason));
        return;
    }
    if (!result_.responsesError.isEmpty())
    {
        responsesNote_->setText(tr("Could not compute the responses: %1").arg(result_.responsesError));
        return;
    }
    const bool moving = support_.timeVarying || support_.stochvol;
    responsesNote_->setText(tr("Responses to a one standard deviation shock to %1%2: the posterior median with 68% "
                               "and 90% bands, %3 draws.")
                                .arg(shock_->currentText(),
                                     moving ? tr(", with the system of %1").arg(period_->currentText()) : QString())
                                .arg(result_.responses.size()));
    if (result_.responses.empty())
    {
        return;
    }
    for (int i = 0; i < responseList_->count(); ++i)
    {
        if (responseList_->item(i)->checkState() == Qt::Checked)
        {
            responseColumns_.push_back(i);
        }
    }
    const int columns = responseColumns_.size() <= 4 ? std::max<int>(1, std::min<int>(2, static_cast<int>(responseColumns_.size()))) : 3;
    const int horizons = static_cast<int>(result_.responses.front().n_rows);
    std::vector<double> x;
    for (int h = 0; h < horizons; ++h)
    {
        x.push_back(h);
    }
    arma::mat stacked(static_cast<arma::uword>(horizons), result_.responses.size());
    for (std::size_t n = 0; n < responseColumns_.size(); ++n)
    {
        const int c = responseColumns_[n];
        for (std::size_t d = 0; d < result_.responses.size(); ++d)
        {
            stacked.col(d) = result_.responses[d].col(static_cast<arma::uword>(c));
        }
        QwtPlot *plot = titledPlot(responseList_->item(c)->text(), tr("Horizon"), responseFigure_,
                                   tr("The response of this series: the posterior median with 68% and 90% bands. "
                                      "Right-click to copy or save the figure."));
        drawPathBands(plot, x, summarisePath(stacked, 1, 0, horizons), 0, true);
        plot->setAxisScale(QwtPlot::xBottom, 0.0, std::max(1.0, double(horizons - 1)));
        plot->replot();
        responseGrid_->addWidget(plot, static_cast<int>(n) / columns, static_cast<int>(n) % columns);
        responsePlots_.push_back(plot);
    }
}

} // namespace studio
