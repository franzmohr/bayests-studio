#include "views/InclusionView.h"

#include "views/PlotStyle.h"

#include <QHeaderView>
#include <QLabel>
#include <QScrollArea>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

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

/// One cell: the probability, shaded by its size, bold from one half on; an
/// element outside the selection greyed out with the reason.
QTableWidgetItem *cell(double p, bool selected, const QString &notSelected)
{
    auto *item = new QTableWidgetItem;
    item->setTextAlignment(Qt::AlignCenter);
    if (!selected)
    {
        item->setText(std::isfinite(p) ? QString::number(p, 'f', 0) : QStringLiteral("–"));
        item->setForeground(QColor(140, 140, 140));
        item->setToolTip(notSelected);
        return item;
    }
    item->setText(QString::number(p, 'f', 2));
    item->setBackground(seriesColor(0, static_cast<int>(20 + 180 * std::clamp(p, 0.0, 1.0))));
    if (p >= 0.5)
    {
        QFont f = item->font();
        f.setBold(true);
        item->setFont(f);
    }
    item->setToolTip(QObject::tr("Posterior inclusion probability %1").arg(p, 0, 'f', 3));
    return item;
}

QTableWidget *table(int rows, int columns, const QStringList &rowNames, const QStringList &columnNames)
{
    auto *t = new QTableWidget(rows, columns);
    t->setHorizontalHeaderLabels(columnNames);
    t->setVerticalHeaderLabels(rowNames);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setSelectionMode(QAbstractItemView::NoSelection);
    t->setFocusPolicy(Qt::NoFocus);
    t->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    t->horizontalHeader()->setMinimumSectionSize(70);
    t->verticalHeader()->setDefaultSectionSize(30);
    t->setFixedHeight(t->horizontalHeader()->sizeHint().height() + 30 * rows + 2 * t->frameWidth() + 2);
    t->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    return t;
}

} // namespace

InclusionView::InclusionView(QWidget *parent) : QWidget(parent)
{
    status_ = subtle();
    host_ = new QWidget;
    tables_ = new QVBoxLayout(host_);
    tables_->setContentsMargins(0, 0, 8, 0);
    tables_->setSpacing(6);
    auto *scroll = new QScrollArea;
    scroll->setWidget(host_);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 8, 0, 0);
    layout->addWidget(status_);
    layout->addWidget(scroll, 1);

    watcher_ = new QFutureWatcher<Result>(this);
    connect(watcher_, &QFutureWatcher<Result>::finished, this, [this]() {
        if (pending_)
        {
            pending_ = false;
            setModel(file_);
            return;
        }
        show(watcher_->result());
    });
    setModel(QString());
}

void InclusionView::clearTables()
{
    while (QLayoutItem *item = tables_->takeAt(0))
    {
        delete item->widget();
        delete item;
    }
}

void InclusionView::setModel(const QString &file)
{
    file_ = file;
    if (file.isEmpty())
    {
        clearTables();
        status_->setText(tr("Select a model with posterior draws."));
        return;
    }
    if (watcher_->isRunning())
    {
        pending_ = true;
        return;
    }
    status_->setText(tr("Reading the draws…"));
    watcher_->setFuture(QtConcurrent::run([file]() {
        Result r;
        try
        {
            r.summary = std::make_shared<CoefficientSummary>(coefficientSummary(file));
        }
        catch (const std::exception &e)
        {
            r.error = QString::fromUtf8(e.what());
        }
        return r;
    }));
}

void InclusionView::show(const Result &result)
{
    clearTables();
    if (!result.error.isEmpty())
    {
        status_->setText(tr("Not available for this model: %1").arg(result.error));
        return;
    }
    const CoefficientSummary &s = *result.summary;
    if (s.selection.isEmpty())
    {
        status_->setText(tr("This model was estimated without variable selection. Choose BVS or SSVS in the "
                            "model builder to obtain inclusion probabilities."));
        return;
    }
    const QString scheme = s.selection == QLatin1String("ssvs")
                               ? tr("Stochastic search variable selection (George, Sun and Ni 2008)")
                               : tr("Bayesian variable selection (Korobilis 2013)");
    int selected = 0, kept = 0;
    for (const CoefficientBlock &b : s.blocks)
    {
        for (const CoefficientPanel &p : b.panels)
        {
            if (p.selected)
            {
                ++selected;
                kept += p.inclusion >= 0.5 ? 1 : 0;
            }
        }
    }
    status_->setText(tr("%1, prior inclusion probability %2, over %3 draws. The share of draws in which each "
                        "coefficient is in the model; in bold from one half on, the median probability model "
                        "(Barbieri and Berger 2004), which keeps %4 of the %5 coefficients selected over.")
                         .arg(scheme)
                         .arg(std::isfinite(s.priorInclusion) ? QString::number(s.priorInclusion, 'g', 3) : tr("—"))
                         .arg(s.draws)
                         .arg(kept)
                         .arg(selected));

    const auto k = static_cast<int>(s.equations.size());
    for (const CoefficientBlock &b : s.blocks)
    {
        if (b.title == QObject::tr("Covariance matrix of the error term"))
        {
            continue;   // its selection is over Psi, below
        }
        const int columns = static_cast<int>(b.columns.size());
        const bool loadings = b.title == QObject::tr("Cointegration matrix");
        const bool contemporaneous = b.title == QObject::tr("Contemporaneous endogenous variables");
        tables_->addWidget(section(b.title));
        auto *t = table(k, columns, s.equations, b.columns);
        for (int j = 0; j < columns; ++j)
        {
            for (int i = 0; i < k; ++i)
            {
                const CoefficientPanel &p = b.panels[static_cast<std::size_t>(j * k + i)];
                const QString why = loadings        ? tr("The loadings are never selected over: the relation is "
                                                         "always in the model.")
                                    : contemporaneous ? tr("Fixed by the recursive form.")
                                                      : tr("Not subject to selection.");
                t->setItem(i, j, cell(p.inclusion, p.selected, why));
            }
        }
        tables_->addWidget(t);
    }
    if (s.psiInclusion.n_elem > 0)
    {
        tables_->addWidget(section(tr("Covariance block (Ψ)")));
        tables_->addWidget(subtle(tr("Row i, column j: the element of the unit lower triangular Ψ that relates "
                                     "the error of i to that of j.")));
        auto *t = table(k, k, s.equations, s.equations);
        for (int i = 0; i < k; ++i)
        {
            for (int j = 0; j < k; ++j)
            {
                const auto iu = static_cast<arma::uword>(i);
                const auto ju = static_cast<arma::uword>(j);
                t->setItem(i, j, cell(s.psiInclusion(iu, ju), s.psiSelected(iu, ju) != 0,
                                      i > j ? tr("Not subject to selection.") : tr("Fixed by the recursive form.")));
            }
        }
        tables_->addWidget(t);
    }
    tables_->addStretch(1);
}

} // namespace studio
