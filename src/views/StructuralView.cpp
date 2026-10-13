// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Franz X. Mohr

#include "views/PeriodBox.h"
#include "views/StructuralView.h"

#include "engine/ModelFileTree.h"
#include "views/FigureExport.h"
#include "views/PlotStyle.h"
#include "views/SeriesPlot.h"
#include "views/SignDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPen>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <qwt_interval.h>
#include <qwt_legend.h>
#include <qwt_plot.h>
#include <qwt_plot_curve.h>
#include <qwt_plot_intervalcurve.h>
#include <qwt_plot_marker.h>
#include <qwt_samples.h>
#include <qwt_text.h>

#include <algorithm>
#include <functional>

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

QwtPlotIntervalCurve *tube(const QString &title, const QColor &color)
{
    auto *curve = new QwtPlotIntervalCurve(title);
    curve->setStyle(QwtPlotIntervalCurve::Tube);
    curve->setPen(QPen(Qt::transparent, 0.0));
    curve->setBrush(color);
    curve->setRenderHint(QwtPlotItem::RenderAntialiased);
    return curve;
}

/// The posterior median of `value(d, h)` over the draws with its 68% and 90%
/// bands, horizon by horizon, replacing what the plot showed.
void drawBands(QwtPlot *plot, int horizons, int draws, const std::function<double(std::size_t, arma::uword)> &value,
               bool zero);

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

void drawBands(QwtPlot *plot, int horizons, int draws, const std::function<double(std::size_t, arma::uword)> &value,
               bool zero)
{
    plot->detachItems(QwtPlotItem::Rtti_PlotCurve, true);
    plot->detachItems(QwtPlotItem::Rtti_PlotIntervalCurve, true);
    plot->detachItems(QwtPlotItem::Rtti_PlotMarker, true);
    if (zero)
    {
        zeroLine(plot);
    }
    QVector<QwtIntervalSample> b90, b68;
    QVector<QPointF> med;
    std::vector<double> column(static_cast<std::size_t>(draws));
    for (int h = 0; h < horizons; ++h)
    {
        for (int d = 0; d < draws; ++d)
        {
            column[static_cast<std::size_t>(d)] = value(static_cast<std::size_t>(d), static_cast<arma::uword>(h));
        }
        std::sort(column.begin(), column.end());
        b90.append({double(h), QwtInterval(sortedQuantile(column, 0.05), sortedQuantile(column, 0.95))});
        b68.append({double(h), QwtInterval(sortedQuantile(column, 0.16), sortedQuantile(column, 0.84))});
        med.append({double(h), sortedQuantile(column, 0.5)});
    }
    auto *t90 = tube(QObject::tr("90%"), seriesColor(0, 50));
    t90->setSamples(b90);
    t90->attach(plot);
    auto *t68 = tube(QObject::tr("68%"), seriesColor(0, 95));
    t68->setSamples(b68);
    t68->attach(plot);
    auto *line = new QwtPlotCurve(QObject::tr("Median"));
    line->setPen(seriesColor(0), 2.0);
    line->setRenderHint(QwtPlotItem::RenderAntialiased);
    line->setSamples(med);
    line->attach(plot);
    plot->setAxisScale(QwtPlot::xBottom, 0.0, std::max(1.0, double(horizons - 1)));
    plot->replot();
}

} // namespace

StructuralView::StructuralView(QWidget *parent) : QWidget(parent)
{
    status_ = subtle();

    type_ = new QComboBox;
    type_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    type_->setMinimumContentsLength(20);
    type_->setToolTip(tr("How the shocks are identified: forecast error (a unit shock to one equation's error), orthogonalised (Cholesky, in the model's variable order), generalised (Pesaran and Shin 1998), structural and structural generalised for a model with contemporaneous coefficients, or the sign restrictions you set."));
    impulse_ = new QComboBox;
    impulse_->setMinimumContentsLength(10);
    impulse_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    impulse_->setToolTip(tr("The variable whose shock the responses trace."));
    horizon_ = new QSpinBox;
    horizon_->setRange(0, 200);
    horizon_->setValue(20);
    horizon_->setToolTip(tr("How many periods after the shock the responses run."));
    period_ = new PeriodBox;
    period_->setToolTip(tr("For a model whose coefficients or volatilities move: the period of the sample whose system the analysis describes. The last period by default."));
    periodLabel_ = new QLabel(tr("Period"));
    cumulative_ = new QCheckBox(tr("Cumulative"));
    cumulative_->setToolTip(tr("The responses summed over the horizons: the response of the level where the model is in growth rates."));
    standardDeviation_ = new QCheckBox(tr("One standard deviation"));
    standardDeviation_->setToolTip(tr("Forecast error and structural responses are to a shock of one unit "
                                      "unless this is ticked; the others are in standard deviations already."));

    auto *controls = new QHBoxLayout;
    controls->addWidget(new QLabel(tr("Identification")));
    controls->addWidget(type_);
    controls->addSpacing(12);
    controls->addWidget(new QLabel(tr("Shock to")));
    controls->addWidget(impulse_);
    controls->addSpacing(12);
    controls->addWidget(new QLabel(tr("Horizon")));
    controls->addWidget(horizon_);
    controls->addSpacing(12);
    controls->addWidget(periodLabel_);
    controls->addWidget(period_);
    controls->addSpacing(12);
    controls->addWidget(cumulative_);
    controls->addWidget(standardDeviation_);
    controls->addStretch(1);
    signButton_ = new QPushButton(tr("Sign Restrictions…"));
    controls->addWidget(signButton_);
    // The figure of the tab shown: the responses, or the decomposition.
    const FigureProvider figure = [this]() {
        FigureSpec spec;
        const ResponseRequest req = request();
        if (tabs_->currentIndex() == 1 && fevdMatrix_)
        {
            // As on screen: a column per shock, a row per variable decomposed.
            spec.title = tr("Forecast error variance decomposition");
            spec.subtitle = fevdNote_->text();
            spec.columns = support_.k;
            for (QwtPlot *p : fevdPlots_)
            {
                spec.cells.push_back({p, {}});
            }
            for (int j = 0; j < support_.k; ++j)
            {
                spec.columnLabels << tr("Shock to %1").arg(impulse_->itemText(j + 1));
            }
            spec.rowLabels = support_.variables;
            spec.name = QStringLiteral("fevd-all");
            return spec;
        }
        if (tabs_->currentIndex() == 1)
        {
            // A panel per shock with its bands, then the stacked means.
            spec.title = tr("Forecast error variance decomposition of %1").arg(response_->currentText());
            spec.subtitle = fevdNote_->text();
            spec.columns = fevdColumns_;
            for (QwtPlot *p : fevdPlots_)
            {
                spec.cells.push_back({p, {}});
            }
            spec.cells.push_back({fevd_, {}});
            spec.name = QStringLiteral("fevd-%1").arg(response_->currentText());
            return spec;
        }
        spec.subtitle = tr("Median, 68% and 90% bands%1").arg(req.cumulative ? tr(", cumulative") : QString());
        spec.columns = columns_;
        for (QwtPlot *p : plots_)
        {
            spec.cells.push_back({p, {}});
        }
        if (matrix_)
        {
            // As on screen: a column per shock, a row per response.
            spec.title = tr("Impulse responses: %1").arg(responseTypeName(req.type).toLower());
            for (int j = 0; j < support_.k; ++j)
            {
                spec.columnLabels << tr("Shock to %1").arg(impulse_->itemText(j + 1));
            }
            spec.rowLabels = support_.variables;
            spec.name = QStringLiteral("irf-%1-all").arg(responseTypeName(req.type));
            return spec;
        }
        spec.title = tr("Responses to %1: %2").arg(impulse_->currentText(), responseTypeName(req.type).toLower());
        spec.name = QStringLiteral("irf-%1-%2").arg(responseTypeName(req.type), impulse_->currentText());
        return spec;
    };
    controls->addWidget(figureExportButton(figure));
    figure_ = figure;
    connect(signButton_, &QPushButton::clicked, this, [this]() {
        SignDialog dialog(file_, support_, this);
        if (dialog.exec() == QDialog::Accepted)
        {
            if (!dialog.createdFile().isEmpty())
            {
                emit modelCreated(dialog.createdFile());
                return;
            }
            setModel(file_);
            const int sign = type_->findData(static_cast<int>(ResponseType::Sign));
            if (sign >= 0)
            {
                type_->setCurrentIndex(sign);
            }
        }
    });

    // Impulse responses: the matrix of every shock, or one plot per response
    // to the shock chosen.
    gridHost_ = new QWidget;
    grid_ = new QGridLayout(gridHost_);
    grid_->setSpacing(12);
    auto *scroll = new QScrollArea;
    irfScroll_ = scroll;
    scroll->setWidget(gridHost_);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    // Variance decomposition of one response.
    auto *fevdPage = new QWidget;
    auto *fevdLayout = new QVBoxLayout(fevdPage);
    fevdLayout->setContentsMargins(0, 8, 0, 0);
    response_ = new QComboBox;
    response_->setObjectName(QStringLiteral("fevdResponse"));
    response_->setToolTip(tr("The variable whose forecast error variance is decomposed into the shares of the shocks."));
    auto *pick = new QHBoxLayout;
    pick->addWidget(new QLabel(tr("Forecast error variance of")));
    pick->addWidget(response_);
    pick->addStretch(1);
    fevdLayout->addLayout(pick);
    fevdNote_ = subtle();
    fevdLayout->addWidget(fevdNote_);
    fevd_ = new QwtPlot;
    styleAxes(fevd_, tr("Horizon"), tr("Share"));
    fevd_->setAxisScale(QwtPlot::yLeft, 0.0, 1.0);
    fevd_->insertLegend(new QwtLegend, QwtPlot::BottomLegend);
    fevd_->setToolTip(tr("The share of the forecast error variance at each horizon due to each shock: posterior means, adding to one. Right-click to copy or save the figure."));
    fevd_->setMinimumHeight(220);
    {
        QwtText title(tr("Posterior means, stacked"));
        QFont f = fevd_->font();
        f.setBold(true);
        title.setFont(f);
        fevd_->setTitle(title);
    }
    // The stacked means sit in the grid below, after a panel per shock.
    fevdGridHost_ = new QWidget;
    fevdGrid_ = new QGridLayout(fevdGridHost_);
    fevdGrid_->setSpacing(12);
    auto *fevdScroll = new QScrollArea;
    fevdScroll->setWidget(fevdGridHost_);
    fevdScroll->setWidgetResizable(true);
    fevdScroll->setFrameShape(QFrame::NoFrame);
    fevdLayout->addWidget(fevdScroll, 1);
    fevdScroll_ = fevdScroll;

    tabs_ = new QTabWidget;
    tabs_->setDocumentMode(true);
    tabs_->addTab(scroll, tr("Impulse responses"));
    tabs_->addTab(fevdPage, tr("Variance decomposition"));
    connect(tabs_, &QTabWidget::currentChanged, this, &StructuralView::reportHeight);
    tabs_->setObjectName(QStringLiteral("structuralTabs"));
    attachFigureExport(fevd_, figure_);

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
    connect(debounce_, &QTimer::timeout, this, &StructuralView::compute);

    for (QComboBox *c : {type_, impulse_, response_})
    {
        connect(c, &QComboBox::currentIndexChanged, this, &StructuralView::schedule);
    }
    connect(horizon_, &QSpinBox::valueChanged, this, &StructuralView::schedule);
    connect(period_, &PeriodBox::valueChanged, this, &StructuralView::schedule);
    for (QCheckBox *c : {cumulative_, standardDeviation_})
    {
        connect(c, &QCheckBox::toggled, this, &StructuralView::schedule);
    }
    connect(type_, &QComboBox::currentIndexChanged, this, [this]() {
        const auto t = static_cast<ResponseType>(type_->currentData().toInt());
        standardDeviation_->setEnabled(t == ResponseType::Feir || t == ResponseType::Sir);
        // Under sign restrictions a shock is named for the variable whose
        // column it is, and only the restricted ones mean anything.
        for (int v = 0; v < support_.k; ++v)
        {
            QString label = support_.variables.value(v);
            if (t == ResponseType::Sign && support_.sign)
            {
                bool restricted = false;
                for (const SignRestriction &r : support_.sign->restrictions)
                {
                    restricted = restricted || r.impulse == v;
                }
                label = restricted ? tr("%1 shock (restricted)").arg(label)
                                   : tr("%1 shock (unrestricted: no interpretation)").arg(label);
            }
            impulse_->setItemText(v + 1, label);
        }
        period_->setEnabled(t != ResponseType::Sign);
        for (int j = 0; matrix_ && j < support_.k && j < static_cast<int>(headers_.size()); ++j)
        {
            if (auto *l = qobject_cast<QLabel *>(headers_[static_cast<std::size_t>(j)]))
            {
                l->setText(tr("Shock to %1").arg(impulse_->itemText(j + 1)));
            }
        }
        for (int j = 0; fevdMatrix_ && j < support_.k && j < static_cast<int>(fevdHeaders_.size()); ++j)
        {
            if (auto *l = qobject_cast<QLabel *>(fevdHeaders_[static_cast<std::size_t>(j)]))
            {
                l->setText(tr("Shock to %1").arg(impulse_->itemText(j + 1)));
            }
        }
        for (int j = 0; !fevdMatrix_ && j < support_.k && j < static_cast<int>(fevdPlots_.size()); ++j)
        {
            QwtText title = fevdPlots_[static_cast<std::size_t>(j)]->title();
            title.setText(tr("Shock to %1").arg(impulse_->itemText(j + 1)));
            fevdPlots_[static_cast<std::size_t>(j)]->setTitle(title);
        }
    });
    connect(response_, &QComboBox::currentIndexChanged, this, [this]() {
        if (support_.ok && allResponses() != fevdMatrix_)
        {
            rebuildFevdGrid(support_.k);
        }
    });
    // All shocks and one need grids of their own.
    connect(impulse_, &QComboBox::currentIndexChanged, this, [this]() {
        if (support_.ok && allShocks() != matrix_)
        {
            rebuildGrid(support_.k);
        }
    });
    setModel(QString());
}

int StructuralView::naturalHeight() const
{
    // The view less its scroll area's viewport is its controls, tab bar and
    // notes as they stand; to that, the grid's rows of panels.
    const bool fevd = tabs_->currentIndex() == 1;
    const auto *scroll = qobject_cast<const QScrollArea *>(fevd ? fevdScroll_ : static_cast<QWidget *>(irfScroll_));
    if (scroll == nullptr || height() <= 0 || !scroll->isVisible())
    {
        return -1;
    }
    const int k = std::max(1, support_.k);
    const bool matrix = fevd ? fevdMatrix_ : matrix_;
    const int columns = std::max(1, fevd ? (matrix ? k : fevdColumns_) : (matrix ? k : columns_));
    const int cells = fevd ? (matrix ? k * k : static_cast<int>(fevdPlots_.size()) + 1)
                           : (matrix ? k * k : static_cast<int>(plots_.size()));
    if (cells == 0)
    {
        return height() - scroll->viewport()->height();
    }
    const int rows = (cells + columns - 1) / columns;
    const QGridLayout *grid = fevd ? fevdGrid_ : grid_;
    const int row = matrix ? 220 : 300;
    int content = rows * row + (rows - 1) * grid->spacing() + 2 * scroll->frameWidth() + 8;
    if (matrix)
    {
        const std::vector<QWidget *> &headers = fevd ? fevdHeaders_ : headers_;
        content += (headers.empty() ? 20 : headers.front()->sizeHint().height()) + grid->spacing();
    }
    // Never less than the grid needs, its panels' minimum heights and all.
    const QWidget *host = fevd ? fevdGridHost_ : gridHost_;
    content = std::max({content, host->minimumSizeHint().height(), host->sizeHint().height()}) +
              2 * scroll->frameWidth() + 16;
    return height() - scroll->viewport()->height() + content;
}

void StructuralView::reportHeight()
{
    // A moment after the change: the panels' axes, the last row's title
    // among them, settle their sizes once replotted.
    QTimer::singleShot(80, this, [this]() { emit naturalHeightChanged(naturalHeight()); });
}

void StructuralView::setModel(const QString &file)
{
    file_ = file;
    support_ = file.isEmpty() ? StructuralSupport{} : structuralSupport(file);
    const bool ok = support_.ok;
    for (QWidget *w : std::initializer_list<QWidget *>{type_, impulse_, horizon_, period_, cumulative_,
                                                        standardDeviation_, tabs_, response_})
    {
        w->setEnabled(ok);
    }
    signButton_->setEnabled(ok && support_.signAllowed);
    signButton_->setToolTip(ok && !support_.signAllowed ? support_.signRefusal
                                                        : tr("Identify the shocks by the signs of their responses."));
    if (!ok)
    {
        status_->setText(file.isEmpty() ? tr("Select a model of the project.")
                                        : tr("Not available for this model: %1").arg(support_.reason));
        rebuildGrid(0);
        rebuildFevdGrid(0);
        return;
    }

    {
        const QSignalBlocker b1(type_);
        const QSignalBlocker b2(impulse_);
        const QSignalBlocker b3(response_);
        const QSignalBlocker b4(period_);
        type_->clear();
        // bvartools' rule: a structural model's shocks are sir/sgir, every
        // other model's feir/oir/gir.
        const std::vector<ResponseType> types =
            support_.structural ? std::vector<ResponseType>{ResponseType::Sir, ResponseType::Sgir}
                                : std::vector<ResponseType>{ResponseType::Oir, ResponseType::Gir, ResponseType::Feir};
        for (const ResponseType t : types)
        {
            type_->addItem(responseTypeName(t), static_cast<int>(t));
        }
        if (support_.sign)
        {
            type_->addItem(responseTypeName(ResponseType::Sign), static_cast<int>(ResponseType::Sign));
        }
        impulse_->clear();
        response_->clear();
        impulse_->addItem(tr("All shocks"));
        for (int v = 0; v < support_.k; ++v)
        {
            impulse_->addItem(support_.variables.value(v));
        }
        // The whole matrix while it stays legible; for a larger model the
        // first shock, the matrix a choice away.
        impulse_->setCurrentIndex(support_.k <= 6 ? 0 : 1);
        response_->addItem(tr("All variables"));
        response_->addItems(support_.variables);
        response_->setCurrentIndex(support_.k <= 6 ? 0 : 1);
        period_->setPeriods(support_.periods, file_);
    }
    if (support_.sign)
    {
        type_->setCurrentIndex(type_->findData(static_cast<int>(ResponseType::Sign)));
    }
    period_->setVisible(support_.moving);
    periodLabel_->setVisible(support_.moving);
    standardDeviation_->setEnabled(support_.structural);
    rebuildGrid(support_.k);
    rebuildFevdGrid(support_.k);
    schedule();
}

ResponseRequest StructuralView::request() const
{
    ResponseRequest r;
    r.type = static_cast<ResponseType>(type_->currentData().toInt());
    r.horizon = horizon_->value();
    r.period = support_.moving ? period_->value() : -1;
    r.cumulative = cumulative_->isChecked();
    r.standardDeviation = standardDeviation_->isChecked();
    return r;
}

void StructuralView::schedule()
{
    if (support_.ok)
    {
        debounce_->start();
    }
}

void StructuralView::compute()
{
    if (!support_.ok)
    {
        return;
    }
    if (watcher_->isRunning())
    {
        pending_ = true;   // one more round once this one is in
        return;
    }
    status_->setText(tr("Computing…"));
    const QString file = file_;
    const ResponseRequest req = request();
    std::vector<int> shocks;
    if (allShocks())
    {
        for (int j = 0; j < support_.k; ++j)
        {
            shocks.push_back(j);
        }
    }
    else
    {
        shocks.push_back(impulse_->currentIndex() - 1);
    }
    std::vector<int> decomposed;
    if (allResponses())
    {
        for (int i = 0; i < support_.k; ++i)
        {
            decomposed.push_back(i);
        }
    }
    else
    {
        decomposed.push_back(response_->currentIndex() - 1);
    }
    watcher_->setFuture(QtConcurrent::run([file, req, shocks, decomposed]() {
        const std::lock_guard<std::recursive_mutex> hdf5(hdf5Mutex());
        Result r;
        try
        {
            r.shocks = shocks;
            for (const int j : shocks)
            {
                r.responses.push_back(impulseResponses(file, req, j));
            }
            if (req.type == ResponseType::Oir || req.type == ResponseType::Sir || req.type == ResponseType::Sign)
            {
                r.decomposed = decomposed;
                for (const int i : decomposed)
                {
                    r.shares.push_back(varianceDecomposition(file, req, i));
                }
            }
        }
        catch (const std::exception &e)
        {
            r.error = QString::fromUtf8(e.what());
        }
        return r;
    }));
}

bool StructuralView::allShocks() const
{
    return impulse_->currentIndex() == 0;
}

void StructuralView::rebuildGrid(int k)
{
    reportHeight();   // measured once the event loop has laid the change out
    // Out of the grid at once: a deferred delete can wait for an outer event
    // loop, and the old panels would stand beside the new ones until then.
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
    matrix_ = k > 0 && allShocks();
    QFont bold = font();
    bold.setBold(true);
    if (matrix_)
    {
        // Laid out as the covariance matrix: shock j's column, response i's
        // row, the headers along the top and the left.
        columns_ = k;
        for (int j = 0; j < k; ++j)
        {
            auto *head = new QLabel(tr("Shock to %1").arg(impulse_->itemText(j + 1)));
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
            row->setToolTip(tr("The responses of %1, to each shock along the row.").arg(support_.variables.value(i)));
            grid_->addWidget(row, i + 1, 0);
            headers_.push_back(row);
            for (int j = 0; j < k; ++j)
            {
                auto *plot = new QwtPlot;
                attachFigureExport(plot, figure_);
                plot->setToolTip(tr("The response of %1 to a shock to %2: the posterior median with 68% and 90% bands. Right-click to copy or save the figure.")
                                     .arg(support_.variables.value(i), support_.variables.value(j)));
                plot->setMinimumHeight(170);
                styleAxes(plot, i + 1 == k ? tr("Horizon") : QString(), QString());
                grid_->addWidget(plot, i + 1, j + 1);
                plots_.push_back(plot);
            }
        }
        return;
    }
    const int columns = k <= 4 ? std::max(1, std::min(k, 2)) : 3;
    columns_ = columns;
    for (int i = 0; i < k; ++i)
    {
        auto *plot = new QwtPlot;
        attachFigureExport(plot, figure_);
        plot->setToolTip(tr("The response of this variable: the posterior median with 68% and 90% bands. Right-click to copy or save the figure."));
        plot->setMinimumHeight(220);
        styleAxes(plot, tr("Horizon"), QString());
        QwtText title(support_.variables.value(i));
        title.setFont(bold);
        plot->setTitle(title);
        grid_->addWidget(plot, i / columns, i % columns);
        plots_.push_back(plot);
    }
}

bool StructuralView::allResponses() const
{
    return response_->currentIndex() == 0;
}

void StructuralView::rebuildFevdGrid(int k)
{
    reportHeight();   // measured once the event loop has laid the change out
    for (QWidget *w : std::vector<QWidget *>(fevdPlots_.begin(), fevdPlots_.end()))
    {
        fevdGrid_->removeWidget(w);
        w->hide();
        w->deleteLater();
    }
    fevdPlots_.clear();
    for (QWidget *h : fevdHeaders_)
    {
        fevdGrid_->removeWidget(h);
        h->hide();
        h->deleteLater();
    }
    fevdHeaders_.clear();
    fevdMatrix_ = k > 0 && allResponses();
    fevdGrid_->removeWidget(fevd_);
    QFont bold = font();
    bold.setBold(true);
    if (!fevdMatrix_)
    {
        // One variable: the share of each shock with its bands, a panel each
        // as the responses to one shock, and the stacked posterior means --
        // bvartools' plot -- last. Bands do not stack: quantiles do not add.
        fevdColumns_ = k + 1 <= 4 ? 2 : 3;
        for (int j = 0; j < k; ++j)
        {
            auto *plot = new QwtPlot;
            attachFigureExport(plot, figure_);
            plot->setToolTip(tr("The share of the shock to %1 in this forecast error variance at each horizon: the posterior median with 68% and 90% bands. Right-click to copy or save the figure.")
                                 .arg(support_.variables.value(j)));
            plot->setMinimumHeight(220);
            styleAxes(plot, tr("Horizon"), tr("Share"));
            plot->setAxisScale(QwtPlot::yLeft, 0.0, 1.0);
            QwtText title(tr("Shock to %1").arg(impulse_->itemText(j + 1)));
            title.setFont(bold);
            plot->setTitle(title);
            fevdGrid_->addWidget(plot, j / fevdColumns_, j % fevdColumns_);
            fevdPlots_.push_back(plot);
        }
        fevd_->setVisible(k > 0);
        if (k > 0)
        {
            fevdGrid_->addWidget(fevd_, k / fevdColumns_, k % fevdColumns_);
        }
        return;
    }
    fevd_->hide();
    fevdColumns_ = k;
    // The responses' matrix: shock j's column, the variable decomposed in
    // row i, so that each row adds up to one.
    for (int j = 0; j < k; ++j)
    {
        auto *head = new QLabel(tr("Shock to %1").arg(impulse_->itemText(j + 1)));
        head->setAlignment(Qt::AlignCenter);
        head->setFont(bold);
        head->setWordWrap(true);
        fevdGrid_->addWidget(head, 0, j + 1);
        fevdHeaders_.push_back(head);
    }
    for (int i = 0; i < k; ++i)
    {
        auto *row = new QLabel(support_.variables.value(i));
        row->setAlignment(Qt::AlignCenter);
        row->setFont(bold);
        row->setMinimumWidth(70);
        row->setToolTip(tr("The forecast error variance of %1, split into the shares of the shocks along the row; they add up to one.")
                            .arg(support_.variables.value(i)));
        fevdGrid_->addWidget(row, i + 1, 0);
        fevdHeaders_.push_back(row);
        for (int j = 0; j < k; ++j)
        {
            auto *plot = new QwtPlot;
            attachFigureExport(plot, figure_);
            plot->setToolTip(tr("The share of the shock to %2 in the forecast error variance of %1 at each horizon: the posterior median with 68% and 90% bands. Right-click to copy or save the figure.")
                                 .arg(support_.variables.value(i), support_.variables.value(j)));
            plot->setMinimumHeight(170);
            styleAxes(plot, i + 1 == k ? tr("Horizon") : QString(), QString());
            plot->setAxisScale(QwtPlot::yLeft, 0.0, 1.0);
            fevdGrid_->addWidget(plot, i + 1, j + 1);
            fevdPlots_.push_back(plot);
        }
    }
}

void StructuralView::show(const Result &result)
{
    reportHeight();   // measured once the event loop has laid the change out
    if (!result.error.isEmpty())
    {
        status_->setText(tr("✗  %1").arg(result.error));
        return;
    }
    const ResponseRequest req = request();
    // A result computed for the other layout is stale; the next one is coming.
    if (result.responses.empty() || (result.shocks.size() > 1) != (matrix_ && support_.k > 1))
    {
        return;
    }
    const int draws = static_cast<int>(result.responses.front().size());
    const QString period = support_.moving ? tr(", at %1").arg(period_->currentText()) : QString();
    status_->setText(matrix_ ? tr("%1 responses, a column per shock and a row per response; median, 68% and 90% "
                                  "bands over %2 draws%3.")
                                   .arg(responseTypeName(req.type))
                                   .arg(draws)
                                   .arg(period)
                             : tr("%1 of %2 shocked; median, 68% and 90% bands over %3 draws%4.")
                                   .arg(responseTypeName(req.type), impulse_->currentText())
                                   .arg(draws)
                                   .arg(period));

    // Impulse responses: plot p is the response of v to shock s.
    const int horizons = req.horizon + 1;
    const std::size_t k = static_cast<std::size_t>(std::max(1, support_.k));
    for (std::size_t p = 0; p < plots_.size(); ++p)
    {
        const std::size_t v = matrix_ ? p / k : p;
        const std::size_t s = matrix_ ? p % k : 0;
        if (s >= result.responses.size())
        {
            continue;
        }
        const std::vector<arma::mat> &shock = result.responses[s];
        drawBands(
            plots_[p], horizons, draws, [&](std::size_t d, arma::uword h) { return shock[d](h, v); }, true);
    }

    // Variance decomposition: the matrix of every variable's, or the
    // posterior mean share of each shock in one variable's, stacked.
    fevd_->detachItems(QwtPlotItem::Rtti_PlotIntervalCurve, true);
    if (result.shares.empty())
    {
        fevdNote_->setText(tr("The variance decomposition is shown for orthogonal shocks (oir, sir, sign "
                              "restrictions), whose shares add up to one; choose one of those."));
        for (QwtPlot *p : fevdPlots_)
        {
            p->detachItems(QwtPlotItem::Rtti_PlotCurve, true);
            p->detachItems(QwtPlotItem::Rtti_PlotIntervalCurve, true);
            p->replot();
        }
        fevd_->replot();
        return;
    }
    if ((result.shares.size() > 1) != (fevdMatrix_ && support_.k > 1))
    {
        return;
    }
    const QString kind = req.type == ResponseType::Sir    ? tr("structural")
                         : req.type == ResponseType::Sign ? tr("sign-identified")
                                                          : tr("orthogonalised");
    // The period too, for a model whose system moves: the shares are that period's.
    const QString over = (req.type == ResponseType::Sign
                              ? tr(", over the %1 identified draws").arg(result.shares.front().size())
                              : QString()) +
                         period;
    if (fevdMatrix_)
    {
        fevdNote_->setText(tr("The share of each %1 shock (columns) in the forecast error variance of each variable "
                              "(rows): median, 68% and 90% bands; each row adds up to one%2.")
                               .arg(kind, over));
        for (std::size_t p = 0; p < fevdPlots_.size(); ++p)
        {
            const std::size_t i = p / k, j = p % k;
            if (i >= result.shares.size())
            {
                continue;
            }
            const std::vector<arma::mat> &variable = result.shares[i];
            drawBands(
                fevdPlots_[p], horizons, static_cast<int>(variable.size()),
                [&](std::size_t d, arma::uword h) { return variable[d](h, j); }, false);
        }
        return;
    }
    fevdNote_->setText(tr("The share of each %1 shock in the forecast error variance of %2: median, 68% and 90% "
                          "bands, and the posterior means stacked, adding to one%3.")
                           .arg(kind, response_->currentText(), over));
    const std::vector<arma::mat> &shares = result.shares.front();
    for (std::size_t j = 0; j < fevdPlots_.size(); ++j)
    {
        drawBands(
            fevdPlots_[j], horizons, static_cast<int>(shares.size()),
            [&](std::size_t d, arma::uword h) { return shares[d](h, j); }, false);
    }
    arma::mat mean(static_cast<arma::uword>(horizons), static_cast<arma::uword>(support_.k), arma::fill::zeros);
    for (const auto &d : shares)
    {
        mean += d;
    }
    mean /= static_cast<double>(shares.size());
    arma::vec lower(static_cast<arma::uword>(horizons), arma::fill::zeros);
    for (int j = 0; j < support_.k; ++j)
    {
        QVector<QwtIntervalSample> band;
        for (int h = 0; h < horizons; ++h)
        {
            const auto hh = static_cast<arma::uword>(h);
            const double top = lower(hh) + mean(hh, static_cast<arma::uword>(j));
            band.append({double(h), QwtInterval(lower(hh), top)});
            lower(hh) = top;
        }
        auto *area = tube(support_.variables.value(j), seriesColor(j, 200, support_.k));
        area->setSamples(band);
        area->attach(fevd_);
    }
    fevd_->setAxisScale(QwtPlot::xBottom, 0.0, std::max(1.0, double(req.horizon)));
    fevd_->replot();
}

} // namespace studio
