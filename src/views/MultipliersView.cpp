#include "views/MultipliersView.h"

#include "engine/ModelFileTree.h"
#include "views/PlotStyle.h"
#include "views/SeriesPlot.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPen>
#include <QScrollArea>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <qwt_interval.h>
#include <qwt_plot.h>
#include <qwt_plot_curve.h>
#include <qwt_plot_intervalcurve.h>
#include <qwt_plot_marker.h>
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

QwtPlotIntervalCurve *tube(const QString &title, const QColor &color)
{
    auto *curve = new QwtPlotIntervalCurve(title);
    curve->setStyle(QwtPlotIntervalCurve::Tube);
    curve->setPen(QPen(Qt::transparent, 0.0));
    curve->setBrush(color);
    curve->setRenderHint(QwtPlotItem::RenderAntialiased);
    return curve;
}

} // namespace

MultipliersView::MultipliersView(QWidget *parent) : QWidget(parent)
{
    status_ = subtle();
    impulse_ = new QComboBox;
    impulse_->setMinimumContentsLength(10);
    impulse_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    type_ = new QComboBox;
    type_->addItem(tr("Permanent: held from period 0"), true);
    type_->addItem(tr("Transitory: period 0 alone"), false);
    type_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    horizon_ = new QSpinBox;
    horizon_->setRange(0, 200);
    horizon_->setValue(20);
    shock_ = new QDoubleSpinBox;
    shock_->setRange(-1e6, 1e6);
    shock_->setDecimals(3);
    shock_->setValue(1.0);
    shock_->setToolTip(tr("The size of the change, in the exogenous variable's own units."));
    cumulative_ = new QCheckBox(tr("Cumulative"));
    period_ = new QSpinBox;
    period_->setMinimum(1);
    periodLabel_ = new QLabel(tr("Period"));

    auto *controls = new QHBoxLayout;
    controls->addWidget(new QLabel(tr("Change in")));
    controls->addWidget(impulse_);
    controls->addSpacing(12);
    controls->addWidget(type_);
    controls->addSpacing(12);
    controls->addWidget(new QLabel(tr("by")));
    controls->addWidget(shock_);
    controls->addSpacing(12);
    controls->addWidget(new QLabel(tr("Horizon")));
    controls->addWidget(horizon_);
    controls->addSpacing(12);
    controls->addWidget(periodLabel_);
    controls->addWidget(period_);
    controls->addSpacing(12);
    controls->addWidget(cumulative_);
    controls->addStretch(1);
    figure_ = [this]() {
        FigureSpec spec;
        const MultiplierRequest req = request();
        spec.title = tr("Dynamic multipliers of a %1 change in %2")
                         .arg(req.permanent ? tr("permanent") : tr("transitory"), impulse_->currentText());
        spec.subtitle = tr("Median, 68% and 90% bands%1").arg(req.cumulative ? tr(", cumulative") : QString());
        spec.columns = columns_;
        for (QwtPlot *p : plots_)
        {
            spec.cells.push_back({p, {}});
        }
        spec.name = QStringLiteral("multipliers-%1").arg(impulse_->currentText());
        return spec;
    };
    controls->addWidget(figureExportButton(figure_));

    gridHost_ = new QWidget;
    grid_ = new QGridLayout(gridHost_);
    grid_->setSpacing(12);
    auto *scroll = new QScrollArea;
    scroll->setWidget(gridHost_);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 8, 0, 0);
    layout->addLayout(controls);
    layout->addWidget(status_);
    layout->addWidget(scroll, 1);

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
    connect(debounce_, &QTimer::timeout, this, &MultipliersView::compute);
    for (QComboBox *c : {impulse_, type_})
    {
        connect(c, &QComboBox::currentIndexChanged, this, &MultipliersView::schedule);
    }
    for (QSpinBox *s : {horizon_, period_})
    {
        connect(s, &QSpinBox::valueChanged, this, &MultipliersView::schedule);
    }
    connect(shock_, &QDoubleSpinBox::valueChanged, this, &MultipliersView::schedule);
    connect(cumulative_, &QCheckBox::toggled, this, &MultipliersView::schedule);
    setModel(QString());
}

void MultipliersView::setModel(const QString &file)
{
    file_ = file;
    support_ = file.isEmpty() ? MultiplierSupport{} : multiplierSupport(file);
    const bool ok = support_.ok;
    for (QWidget *w : std::initializer_list<QWidget *>{impulse_, type_, horizon_, shock_, cumulative_, period_})
    {
        w->setEnabled(ok);
    }
    if (!ok)
    {
        status_->setText(file.isEmpty() ? tr("Select a model of the project.")
                                        : tr("Not available for this model: %1").arg(support_.reason));
        rebuildGrid();
        return;
    }
    {
        const QSignalBlocker b1(impulse_);
        const QSignalBlocker b2(period_);
        impulse_->clear();
        impulse_->addItems(support_.exogenous);
        period_->setMaximum(std::max(1, support_.periods));
        period_->setValue(std::max(1, support_.periods));
    }
    period_->setVisible(support_.moving);
    periodLabel_->setVisible(support_.moving);
    rebuildGrid();
    schedule();
}

MultiplierRequest MultipliersView::request() const
{
    MultiplierRequest r;
    r.impulse = impulse_->currentIndex();
    r.horizon = horizon_->value();
    r.shock = shock_->value();
    r.permanent = type_->currentData().toBool();
    r.cumulative = cumulative_->isChecked();
    r.period = support_.moving ? period_->value() : -1;
    return r;
}

void MultipliersView::schedule()
{
    if (support_.ok)
    {
        debounce_->start();
    }
}

void MultipliersView::compute()
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
    const MultiplierRequest req = request();
    watcher_->setFuture(QtConcurrent::run([file, req]() {
        const std::lock_guard<std::recursive_mutex> hdf5(hdf5Mutex());
        Result r;
        try
        {
            r.draws = dynamicMultipliers(file, req);
        }
        catch (const std::exception &e)
        {
            r.error = QString::fromUtf8(e.what());
        }
        return r;
    }));
}

void MultipliersView::rebuildGrid()
{
    for (QwtPlot *p : plots_)
    {
        p->deleteLater();
    }
    plots_.clear();
    const int k = support_.ok ? support_.k : 0;
    const int columns = k <= 4 ? std::max(1, std::min(k, 2)) : 3;
    columns_ = columns;
    for (int i = 0; i < k; ++i)
    {
        auto *plot = new QwtPlot;
        attachFigureExport(plot, figure_);
        plot->setMinimumHeight(220);
        styleAxes(plot, tr("Horizon"), QString());
        QwtText title(support_.variables.value(i));
        QFont f = plot->font();
        f.setBold(true);
        title.setFont(f);
        plot->setTitle(title);
        grid_->addWidget(plot, i / columns, i % columns);
        plots_.push_back(plot);
    }
}

void MultipliersView::show(const Result &result)
{
    if (!result.error.isEmpty())
    {
        status_->setText(tr("✗  %1").arg(result.error));
        return;
    }
    const MultiplierRequest req = request();
    const int draws = static_cast<int>(result.draws.size());
    status_->setText(tr("%1 change of %2 in %3: the response of each variable, in its own units; median, 68% "
                        "and 90% bands over %4 draws%5.")
                         .arg(req.permanent ? tr("A permanent") : tr("A transitory"))
                         .arg(req.shock)
                         .arg(impulse_->currentText())
                         .arg(draws)
                         .arg(support_.moving ? tr(", at period %1").arg(period_->value()) : QString()));
    const int horizons = req.horizon + 1;
    std::vector<double> column(static_cast<std::size_t>(draws));
    for (std::size_t v = 0; v < plots_.size(); ++v)
    {
        QwtPlot *plot = plots_[v];
        plot->detachItems(QwtPlotItem::Rtti_PlotCurve, true);
        plot->detachItems(QwtPlotItem::Rtti_PlotIntervalCurve, true);
        plot->detachItems(QwtPlotItem::Rtti_PlotMarker, true);
        auto *zero = new QwtPlotMarker;
        zero->setLineStyle(QwtPlotMarker::HLine);
        zero->setYValue(0.0);
        QColor c = plot->palette().color(QPalette::Text);
        c.setAlpha(110);
        zero->setLinePen(c, 1.0);
        zero->attach(plot);
        QVector<QwtIntervalSample> b90, b68;
        QVector<QPointF> med;
        for (int h = 0; h < horizons && draws > 0; ++h)
        {
            for (int d = 0; d < draws; ++d)
            {
                column[static_cast<std::size_t>(d)] =
                    result.draws[static_cast<std::size_t>(d)](static_cast<arma::uword>(h), static_cast<arma::uword>(v));
            }
            std::sort(column.begin(), column.end());
            b90.append({double(h), QwtInterval(sortedQuantile(column, 0.05), sortedQuantile(column, 0.95))});
            b68.append({double(h), QwtInterval(sortedQuantile(column, 0.16), sortedQuantile(column, 0.84))});
            med.append({double(h), sortedQuantile(column, 0.5)});
        }
        auto *t90 = tube(tr("90%"), seriesColor(0, 50));
        t90->setSamples(b90);
        t90->attach(plot);
        auto *t68 = tube(tr("68%"), seriesColor(0, 95));
        t68->setSamples(b68);
        t68->attach(plot);
        auto *line = new QwtPlotCurve(tr("Median"));
        line->setPen(seriesColor(0), 2.0);
        line->setRenderHint(QwtPlotItem::RenderAntialiased);
        line->setSamples(med);
        line->attach(plot);
        plot->setAxisScale(QwtPlot::xBottom, 0.0, std::max(1.0, double(req.horizon)));
        plot->replot();
    }
}

} // namespace studio
