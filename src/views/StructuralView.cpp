#include "views/StructuralView.h"

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

StructuralView::StructuralView(QWidget *parent) : QWidget(parent)
{
    status_ = subtle();

    type_ = new QComboBox;
    impulse_ = new QComboBox;
    impulse_->setMinimumContentsLength(10);
    impulse_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    horizon_ = new QSpinBox;
    horizon_->setRange(0, 200);
    horizon_->setValue(20);
    period_ = new QSpinBox;
    period_->setMinimum(1);
    periodLabel_ = new QLabel(tr("Period"));
    cumulative_ = new QCheckBox(tr("Cumulative"));
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
    connect(signButton_, &QPushButton::clicked, this, [this]() {
        SignDialog dialog(file_, support_, this);
        if (dialog.exec() == QDialog::Accepted)
        {
            setModel(file_);
            const int sign = type_->findData(static_cast<int>(ResponseType::Sign));
            if (sign >= 0)
            {
                type_->setCurrentIndex(sign);
            }
        }
    });

    // Impulse responses: one plot per response, in a grid.
    gridHost_ = new QWidget;
    grid_ = new QGridLayout(gridHost_);
    grid_->setSpacing(12);
    auto *scroll = new QScrollArea;
    scroll->setWidget(gridHost_);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    // Variance decomposition of one response.
    auto *fevdPage = new QWidget;
    auto *fevdLayout = new QVBoxLayout(fevdPage);
    fevdLayout->setContentsMargins(0, 8, 0, 0);
    response_ = new QComboBox;
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
    fevd_->insertLegend(new QwtLegend, QwtPlot::RightLegend);
    fevdLayout->addWidget(fevd_, 1);

    tabs_ = new QTabWidget;
    tabs_->setDocumentMode(true);
    tabs_->addTab(scroll, tr("Impulse responses"));
    tabs_->addTab(fevdPage, tr("Variance decomposition"));
    tabs_->setObjectName(QStringLiteral("structuralTabs"));

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
    for (QSpinBox *s : {horizon_, period_})
    {
        connect(s, &QSpinBox::valueChanged, this, &StructuralView::schedule);
    }
    for (QCheckBox *c : {cumulative_, standardDeviation_})
    {
        connect(c, &QCheckBox::toggled, this, &StructuralView::schedule);
    }
    connect(type_, &QComboBox::currentIndexChanged, this, [this]() {
        const auto t = static_cast<ResponseType>(type_->currentData().toInt());
        standardDeviation_->setEnabled(t == ResponseType::Feir || t == ResponseType::Sir);
        // Under sign restrictions a shock is named for the variable whose
        // column it is, and only the restricted ones mean anything.
        for (int v = 0; v < impulse_->count(); ++v)
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
            impulse_->setItemText(v, label);
        }
        period_->setEnabled(t != ResponseType::Sign);
    });
    setModel(QString());
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
        for (int v = 0; v < support_.k; ++v)
        {
            impulse_->addItem(support_.variables.value(v));
        }
        response_->addItems(support_.variables);
        period_->setMaximum(std::max(1, support_.periods));
        period_->setValue(std::max(1, support_.periods));
    }
    period_->setVisible(support_.moving);
    periodLabel_->setVisible(support_.moving);
    standardDeviation_->setEnabled(support_.structural);
    rebuildGrid(support_.k);
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
    const int impulse = impulse_->currentIndex();
    const int response = response_->currentIndex();
    watcher_->setFuture(QtConcurrent::run([file, req, impulse, response]() {
        Result r;
        try
        {
            r.responses = impulseResponses(file, req, impulse);
            if (req.type == ResponseType::Oir || req.type == ResponseType::Sir || req.type == ResponseType::Sign)
            {
                r.shares = varianceDecomposition(file, req, response);
            }
        }
        catch (const std::exception &e)
        {
            r.error = QString::fromUtf8(e.what());
        }
        return r;
    }));
}

void StructuralView::rebuildGrid(int k)
{
    for (QwtPlot *p : plots_)
    {
        p->deleteLater();
    }
    plots_.clear();
    const int columns = k <= 4 ? std::max(1, std::min(k, 2)) : 3;
    for (int i = 0; i < k; ++i)
    {
        auto *plot = new QwtPlot;
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

void StructuralView::show(const Result &result)
{
    if (!result.error.isEmpty())
    {
        status_->setText(tr("✗  %1").arg(result.error));
        return;
    }
    const ResponseRequest req = request();
    const int draws = static_cast<int>(result.responses.size());
    status_->setText(tr("%1 of %2 shocked; median, 68% and 90% bands over %3 draws%4.")
                         .arg(responseTypeName(req.type), impulse_->currentText())
                         .arg(draws)
                         .arg(support_.moving ? tr(", at period %1").arg(period_->value()) : QString()));

    // Impulse responses.
    const int horizons = req.horizon + 1;
    for (std::size_t v = 0; v < plots_.size(); ++v)
    {
        QwtPlot *plot = plots_[v];
        plot->detachItems(QwtPlotItem::Rtti_PlotCurve, true);
        plot->detachItems(QwtPlotItem::Rtti_PlotIntervalCurve, true);
        plot->detachItems(QwtPlotItem::Rtti_PlotMarker, true);
        zeroLine(plot);
        QVector<QwtIntervalSample> b90, b68;
        QVector<QPointF> med;
        std::vector<double> column(static_cast<std::size_t>(draws));
        for (int h = 0; h < horizons; ++h)
        {
            for (int d = 0; d < draws; ++d)
            {
                column[static_cast<std::size_t>(d)] =
                    result.responses[static_cast<std::size_t>(d)](static_cast<arma::uword>(h), static_cast<arma::uword>(v));
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

    // Variance decomposition: the posterior mean share of each shock, stacked.
    fevd_->detachItems(QwtPlotItem::Rtti_PlotIntervalCurve, true);
    if (result.shares.empty())
    {
        fevdNote_->setText(tr("The variance decomposition is shown for orthogonal shocks (oir, sir, sign "
                              "restrictions), whose shares add up to one; choose one of those."));
        fevd_->replot();
        return;
    }
    fevdNote_->setText(tr("Posterior mean share of each %1 shock in the forecast error variance of %2%3.")
                           .arg(req.type == ResponseType::Sir    ? tr("structural")
                                : req.type == ResponseType::Sign ? tr("sign-identified")
                                                                 : tr("orthogonalised"),
                                response_->currentText(),
                                req.type == ResponseType::Sign
                                    ? tr(", over the %1 identified draws").arg(result.shares.size())
                                    : QString()));
    arma::mat mean(static_cast<arma::uword>(horizons), static_cast<arma::uword>(support_.k), arma::fill::zeros);
    for (const auto &d : result.shares)
    {
        mean += d;
    }
    mean /= static_cast<double>(result.shares.size());
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
        auto *area = tube(support_.variables.value(j), seriesColor(j, 200));
        area->setSamples(band);
        area->attach(fevd_);
    }
    fevd_->setAxisScale(QwtPlot::xBottom, 0.0, std::max(1.0, double(req.horizon)));
    fevd_->replot();
}

} // namespace studio
