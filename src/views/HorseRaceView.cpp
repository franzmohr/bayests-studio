// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Franz X. Mohr

#include "views/HorseRaceView.h"

#include "engine/ModelFileTree.h"
#include "views/FigureExport.h"
#include "views/PlotStyle.h"
#include "views/TableExport.h"

#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPen>
#include <QPushButton>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <qwt_legend.h>
#include <qwt_plot.h>
#include <qwt_plot_curve.h>
#include <qwt_plot_marker.h>

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

QLabel *section(const QString &text)
{
    auto *label = new QLabel(text);
    label->setObjectName(QStringLiteral("sectionTitle"));
    return label;
}

QTableWidget *makeTable()
{
    auto *t = new QTableWidget;
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setSelectionMode(QAbstractItemView::NoSelection);
    t->setFocusPolicy(Qt::NoFocus);
    // Sized to their contents, so a large RMSE scrolls rather than elides.
    t->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    t->horizontalHeader()->setStretchLastSection(true);
    t->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    attachTableExport(t);
    return t;
}

QTableWidgetItem *number(double v, int decimals, bool bold = false)
{
    auto *item = new QTableWidgetItem(std::isfinite(v) ? QString::number(v, 'f', decimals) : QStringLiteral("—"));
    item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    if (bold)
    {
        QFont f = item->font();
        f.setBold(true);
        item->setFont(f);
    }
    return item;
}

} // namespace

HorseRaceView::HorseRaceView(QWidget *parent) : QWidget(parent)
{
    status_ = subtle();
    auto *refresh = new QPushButton(tr("Refresh"));
    connect(refresh, &QPushButton::clicked, this, &HorseRaceView::reload);
    refresh->setToolTip(tr("Reads the race's files again, for runs that finished outside the studio."));
    auto *top = new QHBoxLayout;
    top->addWidget(status_, 1);
    top->addWidget(refresh, 0, Qt::AlignTop);

    score_ = makeTable();
    score_->setObjectName(QStringLiteral("raceScore"));
    variable_ = new QComboBox;
    variable_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    variable_->setToolTip(tr("The variable whose RMSE the table shows."));
    connect(variable_, &QComboBox::currentIndexChanged, this, &HorseRaceView::showRmse);
    rmse_ = makeTable();
    rmse_->setObjectName(QStringLiteral("raceRmse"));

    cumulative_ = new QwtPlot;
    styleAxes(cumulative_, tr("Forecast origin"), tr("Cumulative log score difference"));
    cumulative_->insertLegend(new QwtLegend, QwtPlot::BottomLegend);
    cumulative_->setToolTip(tr("Each model's log score less the benchmark's, summed over the origins up to each one: a rise means the model forecast better there, a jump that one episode decided it. Right-click to copy or save the figure."));
    const FigureProvider figure = [this]() {
        FigureSpec spec = singleFigure(cumulative_, tr("Cumulative log predictive score relative to the benchmark"),
                                       QStringLiteral("horse-race-cumulative-log-score"));
        if (scores_)
        {
            spec.subtitle = tr("Benchmark: %1; above zero, the model has forecast better so far")
                                .arg(scores_->manifest.candidates.value(scores_->manifest.benchmark));
        }
        spec.heightMm = 85.0;
        return spec;
    };
    attachFigureExport(cumulative_, figure);

    auto *tables = new QWidget;
    auto *tl = new QVBoxLayout(tables);
    tl->setContentsMargins(0, 0, 0, 0);
    {
        auto *h = new QHBoxLayout;
        h->addWidget(section(tr("Density forecasts: log predictive score")));
        h->addStretch(1);
        h->addWidget(tableExportButton(score_));
        tl->addLayout(h);
    }
    tl->addWidget(score_, 1);
    {
        auto *h = new QHBoxLayout;
        h->addWidget(section(tr("Point forecasts: RMSE relative to the benchmark")));
        h->addWidget(variable_);
        h->addStretch(1);
        h->addWidget(tableExportButton(rmse_));
        tl->addLayout(h);
    }
    tl->addWidget(rmse_, 1);
    auto *plotHost = new QWidget;
    auto *pl = new QVBoxLayout(plotHost);
    pl->setContentsMargins(0, 0, 0, 0);
    {
        auto *h = new QHBoxLayout;
        h->addWidget(section(tr("When does a model win?")));
        h->addStretch(1);
        h->addWidget(figureExportButton(figure));
        pl->addLayout(h);
    }
    pl->addWidget(cumulative_, 1);
    auto *split = new QSplitter(Qt::Horizontal);
    split->addWidget(tables);
    split->addWidget(plotHost);
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 1);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 8, 0, 0);
    layout->addLayout(top);
    layout->addWidget(split, 1);

    watcher_ = new QFutureWatcher<Result>(this);
    connect(watcher_, &QFutureWatcher<Result>::finished, this, [this]() {
        const Result r = watcher_->result();
        scores_ = r.scores;
        if (!r.error.isEmpty())
        {
            status_->setText(r.error);
        }
        show();
    });
    setFolder(QString());
}

void HorseRaceView::setFolder(const QString &folder)
{
    folder_ = folder;
    reload();
}

void HorseRaceView::reload()
{
    const QString race = folder_.isEmpty() ? QString() : QDir(folder_).filePath(QStringLiteral("horse-race"));
    if (race.isEmpty() || !QFile::exists(QDir(race).filePath(QStringLiteral("horse-race.json"))))
    {
        scores_.reset();
        status_->setText(tr("The project has no horse race. The Forecast Horse Race assistant designs and runs one."));
        show();
        return;
    }
    if (watcher_->isRunning())
    {
        return;
    }
    status_->setText(tr("Scoring…"));
    watcher_->setFuture(QtConcurrent::run([race]() {
        const std::lock_guard<std::recursive_mutex> hdf5(hdf5Mutex());
        Result r;
        try
        {
            r.scores = std::make_shared<HorseRaceScores>(scoreHorseRace(readHorseRace(race)));
        }
        catch (const std::exception &e)
        {
            r.error = QString::fromUtf8(e.what());
        }
        return r;
    }));
}

void HorseRaceView::show()
{
    score_->clear();
    score_->setRowCount(0);
    cumulative_->detachItems(QwtPlotItem::Rtti_PlotCurve, true);
    cumulative_->detachItems(QwtPlotItem::Rtti_PlotMarker, true);
    if (!scores_)
    {
        rmse_->clear();
        rmse_->setRowCount(0);
        variable_->clear();
        cumulative_->replot();
        return;
    }
    const HorseRaceScores &s = *scores_;
    const HorseRaceManifest &m = s.manifest;
    const int n = static_cast<int>(m.candidates.size());
    const int b = m.benchmark;
    int done = 0, total = 0;
    for (int c = 0; c < n; ++c)
    {
        done += s.complete[static_cast<std::size_t>(c)];
        total += static_cast<int>(m.files[static_cast<std::size_t>(c)].size());
    }
    status_->setText(tr("%1 models over %2 forecast origins (%3 to %4), horizon %5; %6 of %7 runs complete. "
                        "Benchmark: %8.")
                         .arg(n)
                         .arg(m.origins.size())
                         .arg(m.origins.value(0), m.origins.value(m.origins.size() - 1))
                         .arg(m.horizon)
                         .arg(done)
                         .arg(total)
                         .arg(m.candidates.value(b)));

    // The log score: the sum over the origins scored by every model, so the
    // totals compare the same periods.
    const auto commonOrigins = [&]() {
        std::vector<arma::uword> idx;
        for (arma::uword o = 0; o < static_cast<arma::uword>(m.origins.size()); ++o)
        {
            bool all = true;
            for (int c = 0; c < n; ++c)
            {
                all = all && std::isfinite(s.logScore[static_cast<std::size_t>(c)](o));
            }
            if (all)
            {
                idx.push_back(o);
            }
        }
        return idx;
    }();
    score_->setColumnCount(4);
    score_->setHorizontalHeaderLabels({tr("Origins"), tr("Log score"), tr("Difference to benchmark"),
                                       tr("Per origin")});
    score_->horizontalHeaderItem(0)->setToolTip(tr("The forecast origins whose run holds a scored forecast."));
    score_->horizontalHeaderItem(1)->setToolTip(tr("The log predictive density of what the held-out periods realised, summed over the horizons and the origins. Larger is better."));
    score_->horizontalHeaderItem(2)->setToolTip(tr("The log score less the benchmark's: a log Bayes factor of the forecasts. Positive favours this model; a few points over a short sample are within noise."));
    score_->horizontalHeaderItem(3)->setToolTip(tr("The log score per origin, which compares races over different numbers of origins."));
    score_->setRowCount(n);
    QStringList rows;
    double best = -INFINITY;
    int bestRow = -1;
    std::vector<double> totals(static_cast<std::size_t>(n), NAN);
    for (int c = 0; c < n; ++c)
    {
        double sum = 0.0;
        for (const arma::uword o : commonOrigins)
        {
            sum += s.logScore[static_cast<std::size_t>(c)](o);
        }
        totals[static_cast<std::size_t>(c)] = commonOrigins.empty() ? NAN : sum;
        if (std::isfinite(totals[static_cast<std::size_t>(c)]) && totals[static_cast<std::size_t>(c)] > best)
        {
            best = totals[static_cast<std::size_t>(c)];
            bestRow = c;
        }
        rows << (c == b ? tr("%1 (benchmark)").arg(m.candidates[c]) : m.candidates[c]);
    }
    for (int c = 0; c < n; ++c)
    {
        const double t = totals[static_cast<std::size_t>(c)];
        auto *count = new QTableWidgetItem(QString::number(commonOrigins.size()));
        count->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        score_->setItem(c, 0, count);
        score_->setItem(c, 1, number(t, 2, c == bestRow));
        score_->setItem(c, 2, number(c == b ? 0.0 : t - totals[static_cast<std::size_t>(b)], 2,
                                     c != b && t > totals[static_cast<std::size_t>(b)]));
        score_->setItem(c, 3, number(commonOrigins.empty() ? NAN : t / static_cast<double>(commonOrigins.size()), 3));
    }
    score_->setVerticalHeaderLabels(rows);
    setTableExportInfo(score_, tr("Log predictive score"),
                       tr("Sum over the %1 forecast origins every model was scored at of the log predictive density "
                          "of the realised path over the horizon of %2. A difference above zero is a log Bayes "
                          "factor of the forecasts in favour of the model; bold: the best model, and models beating "
                          "the benchmark.")
                           .arg(commonOrigins.size())
                           .arg(m.horizon));

    {
        const QSignalBlocker block(variable_);
        const int keep = variable_->currentIndex();
        variable_->clear();
        variable_->addItems(m.variables);
        variable_->setCurrentIndex(keep >= 0 && keep < variable_->count() ? keep : 0);
    }
    showRmse();

    // The cumulative difference, origin by origin.
    for (int c = 0, colour = 0; c < n; ++c)
    {
        if (c == b)
        {
            continue;
        }
        QVector<QPointF> points;
        double sum = 0.0;
        for (const arma::uword o : commonOrigins)
        {
            sum += s.logScore[static_cast<std::size_t>(c)](o) - s.logScore[static_cast<std::size_t>(b)](o);
            points.append({m.originTimes[o], sum});
        }
        auto *curve = new QwtPlotCurve(m.candidates[c]);
        curve->setPen(seriesColor(colour++, 255, n - 1), 2.0);
        curve->setRenderHint(QwtPlotItem::RenderAntialiased);
        curve->setSamples(points);
        curve->attach(cumulative_);
    }
    auto *zero = new QwtPlotMarker;
    zero->setLineStyle(QwtPlotMarker::HLine);
    zero->setYValue(0.0);
    zero->setLinePen(QColor(120, 120, 120), 1.0);
    zero->attach(cumulative_);
    cumulative_->replot();
}

void HorseRaceView::showRmse()
{
    rmse_->clear();
    rmse_->setRowCount(0);
    if (!scores_ || variable_->currentIndex() < 0)
    {
        return;
    }
    const HorseRaceScores &s = *scores_;
    const HorseRaceManifest &m = s.manifest;
    const auto v = static_cast<arma::uword>(variable_->currentIndex());
    const int n = static_cast<int>(m.candidates.size());
    const int b = m.benchmark;
    QStringList columns;
    for (int h = 1; h <= m.horizon; ++h)
    {
        columns << tr("h = %1").arg(h);
    }
    rmse_->setColumnCount(m.horizon);
    rmse_->setHorizontalHeaderLabels(columns);
    rmse_->setRowCount(n);
    QStringList rows;
    for (int c = 0; c < n; ++c)
    {
        rows << (c == b ? tr("%1 (benchmark, RMSE)").arg(m.candidates[c]) : m.candidates[c]);
        for (int h = 0; h < m.horizon; ++h)
        {
            const double own = s.rmse[static_cast<std::size_t>(c)](v, static_cast<arma::uword>(h));
            const double bench = s.rmse[static_cast<std::size_t>(b)](v, static_cast<arma::uword>(h));
            if (c == b)
            {
                rmse_->setItem(c, h, number(own, 3));
            }
            else
            {
                const double ratio = own / bench;
                rmse_->setItem(c, h, number(ratio, 3, ratio < 1.0));
            }
        }
    }
    rmse_->setVerticalHeaderLabels(rows);
    setTableExportInfo(rmse_, tr("RMSE relative to the benchmark: %1").arg(variable_->currentText()),
                       tr("Root mean squared error of the posterior mean forecast over the forecast origins, per "
                          "horizon, as a ratio to the benchmark's, whose own RMSE is in its row. Bold: below one, "
                          "the model forecasts better than the benchmark."),
                       b == 0 ? std::vector<int>{1} : std::vector<int>{});
}

bool HorseRaceView::exportReport(const QString &folder, QString *error)
{
    if (!scores_)
    {
        if (error)
        {
            *error = tr("No horse race to report.");
        }
        return false;
    }
    QDir().mkpath(folder);
    const QDir dir(folder);
    const auto write = [&](const QString &name, const QString &text, bool bom) {
        QFile f(dir.filePath(name));
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
            return false;
        }
        if (bom)
        {
            f.write("\xEF\xBB\xBF");
        }
        f.write(text.toUtf8());
        return true;
    };
    bool ok = write(QStringLiteral("log-score.tex"), toLatex(tableContent(score_), QStringLiteral("race-log-score")), false) &&
              write(QStringLiteral("log-score.csv"), toCsv(tableContent(score_)), true);
    const int keep = variable_->currentIndex();
    for (int v = 0; v < variable_->count(); ++v)
    {
        variable_->setCurrentIndex(v);
        const QString stem = QStringLiteral("rmse-%1").arg(v + 1);
        ok = ok && write(stem + QStringLiteral(".tex"), toLatex(tableContent(rmse_), QStringLiteral("race-") + stem), false) &&
             write(stem + QStringLiteral(".csv"), toCsv(tableContent(rmse_)), true);
    }
    variable_->setCurrentIndex(keep);
    FigureSpec spec = singleFigure(cumulative_, tr("Cumulative log predictive score relative to the benchmark"),
                                   QStringLiteral("cumulative"));
    spec.heightMm = 85.0;
    double w = 0.0, h = 0.0;
    defaultFigureSize(spec, w, h);
    ok = ok && writeFigure(spec, dir.filePath(QStringLiteral("cumulative-log-score.pdf")), w, h, 300, error);
    if (!ok && error && error->isEmpty())
    {
        *error = tr("Cannot write into %1.").arg(folder);
    }
    return ok;
}

} // namespace studio
