#include "views/AssistantDialogs.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpressionValidator>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace studio
{

namespace
{

QLabel *hint(const QString &text)
{
    auto *l = new QLabel(text);
    l->setWordWrap(true);
    l->setObjectName(QStringLiteral("subtle"));
    return l;
}

/// A checkable list of the data set's series, reorderable where `order`.
QListWidget *seriesList(const DataSet &data, const QStringList &checked, bool all)
{
    auto *list = new QListWidget;
    list->setMaximumHeight(150);
    // The checked ones first, in their order, then the rest.
    std::vector<int> order;
    for (const QString &name : checked)
    {
        const int i = static_cast<int>(data.names.indexOf(name));
        if (i >= 0)
        {
            order.push_back(i);
        }
    }
    for (int c = 0; c < data.series(); ++c)
    {
        if (std::find(order.begin(), order.end(), c) == order.end())
        {
            order.push_back(c);
        }
    }
    for (const int c : order)
    {
        auto *item = new QListWidgetItem(data.names[c]);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(all || checked.contains(data.names[c]) ? Qt::Checked : Qt::Unchecked);
        item->setData(Qt::UserRole, c);
        list->addItem(item);
    }
    return list;
}

std::vector<int> checkedColumns(const QListWidget *list)
{
    std::vector<int> out;
    for (int i = 0; i < list->count(); ++i)
    {
        if (list->item(i)->checkState() == Qt::Checked)
        {
            out.push_back(list->item(i)->data(Qt::UserRole).toInt());
        }
    }
    return out;
}

} // namespace

// --------------------------------------------------------------------------
// Thesis baseline

ThesisBaselineDialog::ThesisBaselineDialog(const DataSet &data, const WorkflowExample *example, QWidget *parent)
    : QDialog(parent), data_(data), example_(example)
{
    setWindowTitle(tr("Baseline Model"));
    resize(560, 640);
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(hint(example
                               ? tr("The specification of %1, filled in. Change what you like: the assistant "
                                    "keeps it, and builds the lag and robustness variants from it.")
                                     .arg(example->title)
                               : tr("The recommended baseline: a Bayesian VAR with a constant and the Minnesota "
                                    "prior. The assistant keeps it, and builds the lag and robustness variants "
                                    "from it.")));

    QStringList chosen;
    if (example)
    {
        for (const auto &[source, name] : example->recipe.columns)
        {
            chosen << name;
        }
        if (chosen.isEmpty())
        {
            chosen = example->series;
        }
    }
    variables_ = seriesList(data, chosen, chosen.isEmpty());
    variables_->setObjectName(QStringLiteral("baselineVariables"));
    variables_->setToolTip(tr("The variables of the baseline. Their order is the recursive order of the Cholesky identification: a variable responds within the period to those above it only."));
    auto *up = new QToolButton;
    up->setArrowType(Qt::UpArrow);
    up->setToolTip(tr("Move the selected variable up the ordering."));
    auto *down = new QToolButton;
    down->setArrowType(Qt::DownArrow);
    down->setToolTip(tr("Move the selected variable down the ordering."));
    const auto move = [this](int by) {
        const int row = variables_->currentRow();
        const int to = row + by;
        if (row < 0 || to < 0 || to >= variables_->count())
        {
            return;
        }
        QListWidgetItem *item = variables_->takeItem(row);
        variables_->insertItem(to, item);
        variables_->setCurrentRow(to);
        update();
    };
    connect(up, &QToolButton::clicked, this, [move]() { move(-1); });
    connect(down, &QToolButton::clicked, this, [move]() { move(1); });
    auto *arrows = new QVBoxLayout;
    arrows->addWidget(up);
    arrows->addWidget(down);
    arrows->addStretch(1);
    auto *list = new QHBoxLayout;
    list->addWidget(variables_, 1);
    list->addLayout(arrows);
    layout->addWidget(new QLabel(tr("Variables, in the recursive order: slow-moving first, the policy instrument last")));
    layout->addLayout(list);

    auto *form = new QFormLayout;
    var_ = new QRadioButton(tr("VAR"));
    vec_ = new QRadioButton(tr("VEC, for series in levels with a cointegration relation"));
    var_->setChecked(true);
    vec_->setToolTip(tr("A vector error correction model for series in levels that share a long-run relation."));
    var_->setToolTip(tr("A VAR in the variables as they are: growth rates, or levels with the random walk prior."));
    auto *family = new QButtonGroup(this);
    family->addButton(var_);
    family->addButton(vec_);
    auto *fam = new QVBoxLayout;
    fam->addWidget(var_);
    fam->addWidget(vec_);
    form->addRow(tr("Model"), fam);
    rank_ = new QSpinBox;
    rank_->setRange(1, 10);
    rank_->setToolTip(tr("The number of cointegration relations."));
    form->addRow(tr("Cointegration rank"), rank_);
    p_ = new QSpinBox;
    p_->setRange(1, 24);
    p_->setValue(example ? example->p : suggestedLags(data));
    p_->setToolTip(tr("A year of dynamics: four for quarterly data, twelve for monthly, two for annual."));
    form->addRow(tr("Lags"), p_);
    prior_ = new QComboBox;
    prior_->addItem(tr("Minnesota prior, for growth rates (recommended)"));
    prior_->addItem(tr("Flat: the data alone"));
    prior_->addItem(tr("Minnesota prior, random walks, for series in levels"));
    prior_->setCurrentIndex(example && !example->minnesota ? 1 : 0);
    prior_->setToolTip(tr("The Minnesota prior (Litterman 1986) shrinks towards white noise for growth rates, or towards random walks for series in levels; flat lets the data alone speak, which a small sample with many lags cannot afford."));
    form->addRow(tr("Coefficient prior"), prior_);
    kappa1_ = new QDoubleSpinBox;
    kappa1_->setRange(0.01, 10.0);
    kappa1_->setSingleStep(0.05);
    kappa1_->setValue(0.2);
    kappa1_->setToolTip(tr("Overall tightness of the Minnesota prior: smaller shrinks every coefficient harder towards the prior mean -- white noise, or random walks with the prior for levels."));
    form->addRow(tr("Tightness κ₁"), kappa1_);
    iterations_ = new QSpinBox;
    iterations_->setRange(100, 1000000);
    iterations_->setSingleStep(1000);
    iterations_->setValue(example ? example->iterations : 20000);
    burnin_ = new QSpinBox;
    burnin_->setRange(0, 1000000);
    burnin_->setSingleStep(500);
    burnin_->setValue(example ? example->burnin : 2000);
    burnin_->setToolTip(tr("Draws discarded while the chain moves away from its starting values."));
    iterations_->setToolTip(tr("Draws kept after the burn-in, the size of every posterior summary."));
    form->addRow(tr("Draws kept"), iterations_);
    form->addRow(tr("Burn-in"), burnin_);
    seed_ = new QLineEdit(QString::number(example ? example->seed : 20261011ULL));
    seed_->setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("\\d{0,10}")), seed_));
    seed_->setToolTip(tr("A fixed seed makes the draws, and so every number of the thesis, reproducible."));
    form->addRow(tr("Seed"), seed_);
    layout->addLayout(form);

    summary_ = hint(QString());
    layout->addWidget(summary_);
    layout->addStretch(1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    ok_ = buttons->button(QDialogButtonBox::Ok);
    ok_->setText(tr("Create the Baseline"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(variables_, &QListWidget::itemChanged, this, &ThesisBaselineDialog::update);
    for (QRadioButton *b : {var_, vec_})
    {
        connect(b, &QRadioButton::toggled, this, &ThesisBaselineDialog::update);
    }
    for (QSpinBox *s : {rank_, p_, iterations_, burnin_})
    {
        connect(s, &QSpinBox::valueChanged, this, &ThesisBaselineDialog::update);
    }
    connect(prior_, &QComboBox::currentIndexChanged, this, &ThesisBaselineDialog::update);
    update();
}

VarBuilderSpec ThesisBaselineDialog::spec() const
{
    VarBuilderSpec s;
    s.data = data_;
    s.columns = checkedColumns(variables_);
    s.family = vec_->isChecked() ? VarBuilderSpec::Family::Vec : VarBuilderSpec::Family::Var;
    s.rank = rank_->value();
    s.p = p_->value();
    s.constant = true;
    s.minnesota = prior_->currentIndex() != 1;
    s.randomWalkMean = prior_->currentIndex() == 2;
    s.kappa1 = kappa1_->value();
    if (!s.minnesota)
    {
        s.coefPrecision = example_ ? example_->coefPrecision : 1e-4;
        s.detPrecision = example_ ? example_->detPrecision : 1e-4;
    }
    if (example_)
    {
        s.wishartScale = example_->wishartScale;
        if (example_->wishartDf > 0)
        {
            s.wishartDfExtra = example_->wishartDf - static_cast<int>(s.columns.size());
        }
    }
    s.iterations = iterations_->value();
    s.burnin = burnin_->value();
    if (!seed_->text().isEmpty())
    {
        s.seed = seed_->text().toULongLong();
    }
    s.horizon = 8;   // a forecast to look at; nothing is held out
    return s;
}

void ThesisBaselineDialog::update()
{
    rank_->setEnabled(vec_->isChecked());
    kappa1_->setEnabled(prior_->currentIndex() != 1);
    const VarBuilderSummary s = summarise(spec());
    if (!s.refusal.isEmpty())
    {
        summary_->setText(tr("✗  %1").arg(s.refusal));
    }
    else
    {
        summary_->setText(tr("%1 variables, %2 periods, %3 coefficients.").arg(s.k).arg(s.periods).arg(s.nparams));
    }
    ok_->setEnabled(s.refusal.isEmpty());
}

// --------------------------------------------------------------------------
// Horse race

HorseRaceDialog::HorseRaceDialog(const DataSet &data, const WorkflowExample *example, QWidget *parent)
    : QDialog(parent), data_(data)
{
    setWindowTitle(tr("Design the Horse Race"));
    resize(620, 760);
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(hint(tr("Every model is estimated at every forecast origin on the data up to it -- an "
                              "expanding window -- and forecasts the horizon after it, which is then scored.")));

    variables_ = seriesList(data, example ? example->variables : QStringList(), !example);
    variables_->setToolTip(tr("The variables every model of the race forecasts and is scored on."));
    layout->addWidget(new QLabel(tr("Variables to forecast")));
    layout->addWidget(variables_);

    auto *form = new QFormLayout;
    first_ = new QComboBox;
    firstOrigin_ = new QComboBox;
    lastOrigin_ = new QComboBox;
    for (int r = 0; r < data.length(); ++r)
    {
        first_->addItem(data.periodLabel(r));
        firstOrigin_->addItem(data.periodLabel(r));
        lastOrigin_->addItem(data.periodLabel(r));
    }
    const int last = data.length() - 1;
    const auto at = [&](const QString &label, int fallback) {
        const int i = static_cast<int>(data.periods.indexOf(label));
        return i >= 0 ? i : fallback;
    };
    first_->setCurrentIndex(0);
    firstOrigin_->setCurrentIndex(example ? at(example->firstOrigin, last * 3 / 4) : std::max(0, last - std::max(8, last / 4)));
    lastOrigin_->setCurrentIndex(example ? at(example->lastOrigin, last - 1) : std::max(0, last - 1));
    first_->setToolTip(tr("Where every estimation window starts. The windows expand: each runs from here to its origin."));
    firstOrigin_->setToolTip(tr("The last period of the first window, from which the first forecast is made."));
    lastOrigin_->setToolTip(tr("The last period of the last window. Its forecast is scored on what remains of the data, so it lies before the end."));
    form->addRow(tr("Windows start"), first_);
    form->addRow(tr("First forecast origin"), firstOrigin_);
    form->addRow(tr("Last forecast origin"), lastOrigin_);
    step_ = new QSpinBox;
    step_->setRange(1, 24);
    step_->setValue(example ? example->step : 1);
    step_->setToolTip(tr("Periods between origins: 1 forecasts from every period, 3 from every quarter of "
                         "monthly data."));
    form->addRow(tr("Every"), step_);
    horizon_ = new QSpinBox;
    horizon_->setRange(1, 60);
    horizon_->setValue(example ? example->horizon : 8);
    horizon_->setToolTip(tr("Periods ahead each model forecasts from each origin, and is scored on."));
    form->addRow(tr("Horizon"), horizon_);
    lags_ = new QSpinBox;
    lags_->setRange(1, 24);
    lags_->setValue(example ? example->p : suggestedLags(data));
    lags_->setToolTip(tr("The lag order of the benchmark and the models like it; the one-lag and TVP models keep their own."));
    form->addRow(tr("Lags of the larger VARs"), lags_);
    levels_ = new QCheckBox(tr("The series are in levels: random walk priors, and a VEC"));
    levels_->setChecked(example && example->levels);
    levels_->setToolTip(tr("For series in levels: the Minnesota priors centre on random walks (Litterman 1986), and a VEC of rank one joins the field."));
    form->addRow(QString(), levels_);
    break_ = new QComboBox;
    break_->addItem(tr("None"));
    for (int r = 0; r < data.length(); ++r)
    {
        break_->addItem(data.periodLabel(r));
    }
    break_->setCurrentIndex(example && !example->volatilityBreak.isEmpty() ? at(example->volatilityBreak, -1) + 1 : 0);
    break_->setToolTip(tr("Adds the benchmark with the volatility break of Lenza and Primiceri (2022) from this "
                          "period -- March 2020 for monthly data through the pandemic, 2020Q2 for quarterly."));
    form->addRow(tr("Volatility break from"), break_);
    layout->addLayout(form);

    layout->addWidget(new QLabel(tr("The field")));
    candidates_ = new QListWidget;
    candidates_->setObjectName(QStringLiteral("raceCandidates"));
    candidates_->setMaximumHeight(170);
    candidates_->setToolTip(tr("The models in the race; hover over one for what it is. Each is estimated at every origin."));
    layout->addWidget(candidates_);
    auto *bench = new QFormLayout;
    benchmark_ = new QComboBox;
    benchmark_->setToolTip(tr("The model the others are measured against: RMSEs relative to its, log scores as differences from its."));
    bench->addRow(tr("Benchmark"), benchmark_);
    iterations_ = new QSpinBox;
    iterations_->setRange(100, 100000);
    iterations_->setSingleStep(500);
    iterations_->setValue(example ? example->iterations : 2000);
    burnin_ = new QSpinBox;
    burnin_->setRange(0, 100000);
    burnin_->setSingleStep(250);
    burnin_->setValue(example ? example->burnin : 500);
    iterations_->setToolTip(tr("Draws kept per run. A race estimates every model at every origin, so fewer draws than for a single model are usual."));
    burnin_->setToolTip(tr("Draws discarded at the start of each run."));
    bench->addRow(tr("Draws kept per run"), iterations_);
    bench->addRow(tr("Burn-in"), burnin_);
    layout->addLayout(bench);

    summary_ = hint(QString());
    layout->addWidget(summary_);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    ok_ = buttons->button(QDialogButtonBox::Ok);
    ok_->setText(tr("Write the Race"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    rebuildCandidates();
    if (example && !example->candidates.empty())
    {
        for (int i = 0; i < candidates_->count(); ++i)
        {
            const bool in = std::find(example->candidates.begin(), example->candidates.end(), i) != example->candidates.end();
            candidates_->item(i)->setCheckState(in ? Qt::Checked : Qt::Unchecked);
        }
    }
    connect(variables_, &QListWidget::itemChanged, this, &HorseRaceDialog::update);
    connect(candidates_, &QListWidget::itemChanged, this, &HorseRaceDialog::update);
    for (QComboBox *c : {first_, firstOrigin_, lastOrigin_, benchmark_})
    {
        connect(c, &QComboBox::currentIndexChanged, this, &HorseRaceDialog::update);
    }
    for (QSpinBox *s : {step_, horizon_, iterations_, burnin_})
    {
        connect(s, &QSpinBox::valueChanged, this, &HorseRaceDialog::update);
    }
    connect(lags_, &QSpinBox::valueChanged, this, &HorseRaceDialog::rebuildCandidates);
    connect(levels_, &QCheckBox::toggled, this, &HorseRaceDialog::rebuildCandidates);
    connect(break_, &QComboBox::currentIndexChanged, this, &HorseRaceDialog::rebuildCandidates);
    update();
}

void HorseRaceDialog::rebuildCandidates()
{
    std::vector<bool> keep;
    for (int i = 0; i < candidates_->count(); ++i)
    {
        keep.push_back(candidates_->item(i)->checkState() == Qt::Checked);
    }
    field_ = standardCandidates(lags_->value(), levels_->isChecked(), break_->currentIndex() - 1,
                                break_->currentText());
    {
        const QSignalBlocker block(candidates_);
        candidates_->clear();
        for (std::size_t i = 0; i < field_.size(); ++i)
        {
            auto *item = new QListWidgetItem(field_[i].name);
            item->setToolTip(field_[i].description);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            // The slow TVP-VAR is left for the user to add.
            const bool on = i < keep.size() ? keep[i] : field_[i].spec.timeVarying == false;
            item->setCheckState(on ? Qt::Checked : Qt::Unchecked);
            candidates_->addItem(item);
        }
    }
    update();
}

HorseRaceDesign HorseRaceDialog::design() const
{
    HorseRaceDesign d;
    d.data = data_;
    d.columns = checkedColumns(variables_);
    d.firstRow = first_->currentIndex();
    d.firstOrigin = firstOrigin_->currentIndex();
    d.lastOrigin = lastOrigin_->currentIndex();
    d.step = step_->value();
    d.horizon = horizon_->value();
    d.iterations = iterations_->value();
    d.burnin = burnin_->value();
    const QString bench = benchmark_->currentText();
    for (int i = 0; i < candidates_->count(); ++i)
    {
        if (candidates_->item(i)->checkState() == Qt::Checked)
        {
            if (field_[static_cast<std::size_t>(i)].name == bench)
            {
                d.benchmark = static_cast<int>(d.candidates.size());
            }
            d.candidates.push_back(field_[static_cast<std::size_t>(i)]);
        }
    }
    return d;
}

void HorseRaceDialog::update()
{
    {
        const QString keep = benchmark_->currentText();
        const QSignalBlocker block(benchmark_);
        benchmark_->clear();
        for (int i = 0; i < candidates_->count(); ++i)
        {
            if (candidates_->item(i)->checkState() == Qt::Checked)
            {
                benchmark_->addItem(candidates_->item(i)->text());
            }
        }
        const int at = benchmark_->findText(keep);
        benchmark_->setCurrentIndex(at >= 0 ? at : 0);
    }
    const HorseRaceDesign d = design();
    const QString why = horseRaceRefusal(d);
    const int runs = static_cast<int>(d.candidates.size() * d.origins().size());
    if (!why.isEmpty())
    {
        summary_->setText(tr("✗  %1").arg(why));
    }
    else
    {
        summary_->setText(tr("%1 models × %2 origins = %3 estimations, each forecasting %4 periods. They run "
                             "several at a time, one per processor core.")
                              .arg(d.candidates.size())
                              .arg(d.origins().size())
                              .arg(runs)
                              .arg(d.horizon));
    }
    ok_->setEnabled(why.isEmpty());
}

} // namespace studio
