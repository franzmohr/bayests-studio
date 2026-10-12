#include "views/BuilderDialog.h"

#include "engine/ModelRunner.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QDoubleValidator>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QStandardItemModel>
#include <QScrollArea>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <limits>

namespace studio
{

namespace
{

QDoubleSpinBox *number(double value, double min, double max, int decimals = 3)
{
    auto *box = new QDoubleSpinBox;
    box->setRange(min, max);
    box->setDecimals(decimals);
    box->setValue(value);
    box->setSingleStep(value > 0 ? value / 10.0 : 0.1);
    return box;
}

/// Small rates (1e-4) are unreadable in a spin box; a validated line edit
/// takes them in scientific notation.
QLineEdit *small(double value)
{
    auto *edit = new QLineEdit(QString::number(value, 'g', 6));
    auto *validator = new QDoubleValidator(0.0, std::numeric_limits<double>::max(), 12, edit);
    validator->setNotation(QDoubleValidator::ScientificNotation);
    edit->setValidator(validator);
    edit->setMaximumWidth(120);
    return edit;
}

double valueOf(const QLineEdit *edit)
{
    return edit->text().toDouble();
}

QLabel *hint(const QString &text)
{
    auto *label = new QLabel(text);
    label->setObjectName(QStringLiteral("subtle"));
    label->setWordWrap(true);
    return label;
}

} // namespace

BuilderDialog::BuilderDialog(const DataSet &data, const QString &projectDir, QWidget *parent)
    : QDialog(parent), data_(data), projectDir_(projectDir)
{
    setWindowTitle(tr("New Model"));
    resize(1120, 780);

    auto *form = new QWidget;
    auto *formLayout = new QVBoxLayout(form);
    formLayout->addWidget(buildDataBox());
    formLayout->addWidget(buildSpecBox());
    formLayout->addWidget(buildPriorBox());
    formLayout->addWidget(buildSamplerBox());
    formLayout->addStretch(1);

    auto *scroll = new QScrollArea;
    scroll->setWidget(form);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto *row = new QHBoxLayout;
    row->addWidget(scroll, 3);
    row->addWidget(buildSummary(), 2);

    auto *buttons = new QDialogButtonBox;
    createButton_ = buttons->addButton(tr("Create Model…"), QDialogButtonBox::AcceptRole);
    buttons->addButton(QDialogButtonBox::Cancel);
    connect(createButton_, &QPushButton::clicked, this, &BuilderDialog::create);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(row, 1);
    layout->addWidget(buttons);

    debounce_ = new QTimer(this);
    debounce_->setSingleShot(true);
    debounce_->setInterval(250);
    connect(debounce_, &QTimer::timeout, this, &BuilderDialog::runCheck);

    // Every input reschedules the check.
    for (QSpinBox *w : findChildren<QSpinBox *>())
    {
        connect(w, &QSpinBox::valueChanged, this, &BuilderDialog::scheduleCheck);
    }
    for (QDoubleSpinBox *w : findChildren<QDoubleSpinBox *>())
    {
        connect(w, &QDoubleSpinBox::valueChanged, this, &BuilderDialog::scheduleCheck);
    }
    for (QCheckBox *w : findChildren<QCheckBox *>())
    {
        connect(w, &QCheckBox::toggled, this, &BuilderDialog::scheduleCheck);
    }
    for (QRadioButton *w : findChildren<QRadioButton *>())
    {
        connect(w, &QRadioButton::toggled, this, &BuilderDialog::scheduleCheck);
    }
    for (QComboBox *w : findChildren<QComboBox *>())
    {
        connect(w, &QComboBox::currentIndexChanged, this, &BuilderDialog::scheduleCheck);
    }
    for (QLineEdit *w : findChildren<QLineEdit *>())
    {
        connect(w, &QLineEdit::textChanged, this, &BuilderDialog::scheduleCheck);
    }
    // A series is endogenous or exogenous, never both: ticking it in one list
    // unticks it in the other.
    const auto exclusive = [this](QListWidget *from, QListWidget *other) {
        connect(from, &QListWidget::itemChanged, this, [this, other](QListWidgetItem *item) {
            if (item->checkState() == Qt::Checked)
            {
                const QSignalBlocker block(other);
                for (int i = 0; i < other->count(); ++i)
                {
                    if (other->item(i)->data(Qt::UserRole) == item->data(Qt::UserRole))
                    {
                        other->item(i)->setCheckState(Qt::Unchecked);
                    }
                }
            }
            scheduleCheck();
        });
    };
    exclusive(variables_, exogenous_);
    exclusive(exogenous_, variables_);

    updateEnabled();
    runCheck();
}

QWidget *BuilderDialog::buildDataBox()
{
    auto *box = new QGroupBox(tr("Data"));
    auto *layout = new QVBoxLayout(box);
    layout->addWidget(hint(tr("Tick the endogenous variables and put them in order: the order is the "
                              "recursive ordering of a structural form or a covariance block. Series "
                              "ticked as weakly exogenous enter as regressors the model does not "
                              "explain, and a forecast is conditional on their values.")));

    variables_ = new QListWidget;
    variables_->setMaximumHeight(130);
    variables_->setToolTip(tr("The variables the model explains. Their order is the recursive ordering of a structural form, a covariance block and the orthogonalised impulse responses; move one with the arrows."));
    for (int c = 0; c < data_.series(); ++c)
    {
        auto *item = new QListWidgetItem(data_.names[c]);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Checked);
        item->setData(Qt::UserRole, c);
        variables_->addItem(item);
    }
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
        scheduleCheck();
    };
    connect(up, &QToolButton::clicked, this, [move]() { move(-1); });
    connect(down, &QToolButton::clicked, this, [move]() { move(1); });
    auto *arrows = new QVBoxLayout;
    arrows->addWidget(up);
    arrows->addWidget(down);
    arrows->addStretch(1);
    exogenous_ = new QListWidget;
    exogenous_->setObjectName(QStringLiteral("exogenousList"));
    exogenous_->setMaximumHeight(130);
    exogenous_->setToolTip(tr("Regressors the model does not explain (a VARX or VECX). A forecast takes their future values as given, so it needs them over the horizon."));
    for (int c = 0; c < data_.series(); ++c)
    {
        auto *item = new QListWidgetItem(data_.names[c]);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);
        item->setData(Qt::UserRole, c);
        exogenous_->addItem(item);
    }
    auto *endoColumn = new QVBoxLayout;
    endoColumn->addWidget(new QLabel(tr("Endogenous")));
    auto *endoRow = new QHBoxLayout;
    endoRow->addWidget(variables_, 1);
    endoRow->addLayout(arrows);
    endoColumn->addLayout(endoRow);
    auto *exoColumn = new QVBoxLayout;
    exoColumn->addWidget(new QLabel(tr("Weakly exogenous")));
    exoColumn->addWidget(exogenous_);
    auto *list = new QHBoxLayout;
    list->addLayout(endoColumn, 1);
    list->addSpacing(12);
    list->addLayout(exoColumn, 1);
    layout->addLayout(list);

    first_ = new QComboBox;
    last_ = new QComboBox;
    for (int r = 0; r < data_.length(); ++r)
    {
        first_->addItem(data_.periodLabel(r));
        last_->addItem(data_.periodLabel(r));
    }
    first_->setCurrentIndex(0);
    last_->setCurrentIndex(data_.length() - 1);
    first_->setToolTip(tr("The first period of the sample. The first p periods supply the initial lags and are not themselves explained."));
    last_->setToolTip(tr("The last period used. Periods held out for evaluation are taken from the end of this range."));
    auto *range = new QHBoxLayout;
    range->addWidget(new QLabel(tr("Sample from")));
    range->addWidget(first_);
    range->addWidget(new QLabel(tr("to")));
    range->addWidget(last_);
    range->addStretch(1);
    layout->addLayout(range);
    return box;
}

QWidget *BuilderDialog::buildSpecBox()
{
    auto *box = new QGroupBox(tr("Specification"));
    auto *form = new QFormLayout(box);

    familyVar_ = new QRadioButton(tr("Vector autoregression (VAR)"));
    familyVec_ = new QRadioButton(tr("Vector error correction (VEC)"));
    familyVec_->setObjectName(QStringLiteral("familyVec"));
    familyVar_->setChecked(true);
    familyVar_->setToolTip(tr("A VAR in the variables as they are: levels, growth rates or a mix."));
    familyVec_->setToolTip(tr("A vector error correction model: the variables in levels, cointegrated, estimated in differences with the error correction term, and forecast in levels."));
    auto *familyGroup = new QButtonGroup(box);
    familyGroup->addButton(familyVar_);
    familyGroup->addButton(familyVec_);
    auto *family = new QHBoxLayout;
    family->addWidget(familyVar_);
    family->addWidget(familyVec_);
    family->addStretch(1);
    form->addRow(tr("Model"), family);
    form->addRow(QString(), hint(tr("A VEC takes the variables in levels, is estimated in differences "
                                     "and forecast in levels. Factor, quantile, discounted and "
                                     "unobserved components models follow.")));

    rank_ = new QSpinBox;
    rank_->setRange(1, 1);
    rank_->setValue(1);
    rank_->setToolTip(tr("The number of cointegration relations, between 1 and the number of variables less one."));
    form->addRow(tr("Cointegration rank (r)"), rank_);

    p_ = new QSpinBox;
    p_->setRange(1, 24);
    p_->setValue(2);
    p_->setToolTip(tr("For a VEC this is the order of the VAR in levels, so a VEC of order 1 "
                      "has no lagged differences."));
    form->addRow(tr("Lags (p)"), p_);

    exoLags_ = new QSpinBox;
    exoLags_->setRange(0, 24);
    exoLags_->setValue(1);
    exoLags_->setObjectName(QStringLiteral("exoLags"));
    exoLags_->setToolTip(tr("The exogenous variables enter at lags 0 to s. For a VEC this is a level "
                            "order: their differences at lags 0 to s - 1, and their level at lag 1 in "
                            "the cointegration space, so it is at least 1."));
    form->addRow(tr("Exogenous lags (s)"), exoLags_);

    constant_ = new QCheckBox(tr("Constant"));
    constant_->setChecked(true);
    constant_->setToolTip(tr("An intercept in every equation."));
    trend_ = new QCheckBox(tr("Linear trend"));
    trend_->setToolTip(tr("A linear time trend in every equation."));
    auto *det = new QHBoxLayout;
    det->addWidget(constant_);
    det->addWidget(trend_);
    det->addStretch(1);
    form->addRow(tr("Deterministic terms"), det);
    restrictConstant_ = new QCheckBox(tr("Constant inside the cointegration space"));
    restrictTrend_ = new QCheckBox(tr("Trend inside the cointegration space"));
    restrictTrend_->setChecked(true);
    restrictConstant_->setToolTip(tr("Puts the constant inside the cointegration relations rather than in the differences: no drift in the levels."));
    restrictTrend_->setToolTip(tr("Puts the trend inside the cointegration relations, so the levels can trend without the differences doing so -- the usual choice with a trend."));
    form->addRow(QString(), restrictConstant_);
    form->addRow(QString(), restrictTrend_);

    coefConstant_ = new QRadioButton(tr("Constant"));
    coefTvp_ = new QRadioButton(tr("Time-varying (random walk)"));
    coefTvp_->setObjectName(QStringLiteral("coefTvp"));
    coefConstant_->setChecked(true);
    coefConstant_->setToolTip(tr("One set of coefficients for the whole sample."));
    coefTvp_->setToolTip(tr("Coefficients that drift as random walks (Primiceri 2005): estimated in every period, the path drawn by a simulation smoother."));
    auto *coefGroup = new QButtonGroup(box);
    coefGroup->addButton(coefConstant_);
    coefGroup->addButton(coefTvp_);
    auto *coef = new QHBoxLayout;
    coef->addWidget(coefConstant_);
    coef->addWidget(coefTvp_);
    coef->addStretch(1);
    form->addRow(tr("Coefficients"), coef);

    nonCentred_ = new QCheckBox(tr("Non-centred random walks (test for time variation)"));
    nonCentred_->setToolTip(tr("Frühwirth-Schnatter and Wagner (2010): writes the Savage–Dickey "
                               "ordinates of Chan (2018) for whether each state moves at all."));
    form->addRow(QString(), nonCentred_);

    errWishart_ = new QRadioButton(tr("Wishart"));
    errGamma_ = new QRadioButton(tr("Gamma"));
    errSv_ = new QRadioButton(tr("Stochastic volatility"));
    errSv_->setObjectName(QStringLiteral("errSv"));
    errWishart_->setChecked(true);
    errWishart_->setToolTip(tr("An unrestricted error covariance with an inverse Wishart prior, constant over time."));
    errGamma_->setToolTip(tr("Gamma priors on the error precisions: a diagonal covariance, or with the covariance block the triangular decomposition of Primiceri (2005)."));
    errSv_->setToolTip(tr("Stochastic volatility: each error variance a log random walk, drawn with the mixture of Omori, Chib, Shephard and Nakajima (2007), so the bands widen after volatile periods."));
    auto *errGroup = new QButtonGroup(box);
    errGroup->addButton(errWishart_);
    errGroup->addButton(errGamma_);
    errGroup->addButton(errSv_);
    auto *err = new QHBoxLayout;
    err->addWidget(errWishart_);
    err->addWidget(errGamma_);
    err->addWidget(errSv_);
    err->addStretch(1);
    form->addRow(tr("Error precision"), err);

    covar_ = new QCheckBox(tr("Covariance block (Primiceri 2005)"));
    covar_->setObjectName(QStringLiteral("covar"));
    covar_->setToolTip(tr("Free covariances between the errors, as the lower triangle of the decomposition of Primiceri (2005). Without it the errors of a gamma or SV model are independent."));
    structural_ = new QCheckBox(tr("Structural form (contemporaneous coefficients)"));
    structural_->setToolTip(tr("Estimates the contemporaneous coefficients A_0 in the recursive order of the variables. Needs a diagonal error covariance: gamma or SV errors without the covariance block."));
    form->addRow(QString(), covar_);
    form->addRow(QString(), structural_);

    selection_ = new QComboBox;
    selection_->setObjectName(QStringLiteral("selection"));
    selection_->addItem(tr("None"));
    selection_->addItem(tr("BVS — Korobilis (2013)"));
    selection_->addItem(tr("SSVS — George, Sun and Ni (2008)"));
    selection_->setToolTip(tr("Variable selection: each coefficient is in or out draw by draw, and the share of draws it is in is its posterior inclusion probability. BVS is Korobilis (2013); SSVS is George, Sun and Ni (2008), for constant coefficients and variances only."));
    form->addRow(tr("Variable selection"), selection_);
    inprior_ = number(0.5, 0.0, 1.0, 2);
    inprior_->setSingleStep(0.05);
    inprior_->setToolTip(tr("The prior probability that each coefficient is in the model."));
    form->addRow(tr("Prior inclusion probability"), inprior_);
    selectDet_ = new QCheckBox(tr("Also select deterministic terms"));
    selectCovar_ = new QCheckBox(tr("Also select covariances"));
    selectDet_->setToolTip(tr("Selection reaches the constant and the trend too; by default they are always in."));
    selectCovar_->setToolTip(tr("Selection reaches the covariance block as well. Selecting a covariance to zero changes what a generalised impulse response means."));
    form->addRow(QString(), selectDet_);
    form->addRow(QString(), selectCovar_);

    for (QAbstractButton *b : {static_cast<QAbstractButton *>(familyVar_), static_cast<QAbstractButton *>(familyVec_),
                               static_cast<QAbstractButton *>(constant_), static_cast<QAbstractButton *>(trend_),
                               static_cast<QAbstractButton *>(coefConstant_), static_cast<QAbstractButton *>(coefTvp_),
                               static_cast<QAbstractButton *>(errWishart_), static_cast<QAbstractButton *>(errGamma_),
                               static_cast<QAbstractButton *>(errSv_), static_cast<QAbstractButton *>(covar_),
                               static_cast<QAbstractButton *>(structural_)})
    {
        connect(b, &QAbstractButton::toggled, this, &BuilderDialog::updateEnabled);
    }
    connect(selection_, &QComboBox::currentIndexChanged, this, &BuilderDialog::updateEnabled);
    return box;
}

QWidget *BuilderDialog::buildPriorBox()
{
    auto *box = new QGroupBox(tr("Priors"));
    auto *layout = new QVBoxLayout(box);
    layout->addWidget(hint(tr("Defaults are those of the bvartools article's replication script.")));

    auto *coef = new QFormLayout;
    minnesota_ = new QCheckBox(tr("Minnesota prior (Appendix A of the article)"));
    coef->addRow(QString(), minnesota_);
    minnesota_->setToolTip(tr("The Minnesota prior (Litterman 1986; Appendix A of the bvartools article): tighter on longer lags and on other variables' lags, scaled by each variable's residual variance."));
    coefPrecision_ = number(1.0, 1e-6, 1e6, 4);
    detPrecision_ = number(0.1, 1e-6, 1e6, 4);
    coefPrecision_->setToolTip(tr("Without the Minnesota prior: the prior precision, one over the variance, of every lag coefficient, all centred on zero."));
    detPrecision_->setToolTip(tr("The prior precision of the constant and the trend."));
    coef->addRow(tr("Coefficient precision"), coefPrecision_);
    coef->addRow(tr("Deterministic precision"), detPrecision_);
    kappa1_ = number(0.2, 1e-6, 1e6, 4);
    kappa2_ = number(0.5, 1e-6, 1e6, 4);
    kappa4_ = number(100.0, 1e-6, 1e9, 2);
    kappa1_->setToolTip(tr("κ₁, the overall tightness: the prior variance of a variable's own first lag. Smaller shrinks every coefficient harder."));
    kappa2_->setToolTip(tr("κ₂, the tightness on other variables' lags relative to own lags: below one shrinks them harder."));
    kappa4_->setToolTip(tr("κ₄, the looseness on the deterministic terms relative to κ₁: large leaves the constant nearly unrestricted."));
    coef->addRow(tr("κ₁ overall tightness"), kappa1_);
    coef->addRow(tr("κ₂ other lags"), kappa2_);
    coef->addRow(tr("κ₄ deterministic"), kappa4_);
    randomWalk_ = new QCheckBox(tr("Centre own first lags on one (random walk), for series in levels"));
    randomWalk_->setObjectName(QStringLiteral("randomWalk"));
    randomWalk_->setToolTip(tr("Litterman's prior for variables in levels; leave it off for growth rates and "
                               "other stationary series, which it centres on white noise."));
    coef->addRow(QString(), randomWalk_);
    layout->addLayout(coef);
    connect(minnesota_, &QCheckBox::toggled, this, &BuilderDialog::updateEnabled);

    cointBox_ = new QGroupBox(tr("Cointegration space"));
    auto *coint = new QFormLayout(cointBox_);
    cointShrinkage_ = number(0.1, 0.0, 1e6, 4);
    cointShrinkage_->setToolTip(tr("v of Koop, León-González and Strachan (2010); zero is the flat "
                                   "prior on the loadings with a uniform prior on the space."));
    cointPTauInv_ = number(1.0, 1e-6, 1e6, 4);
    cointRho_ = number(0.999, 0.0, 0.999999, 6);
    cointRho_->setSingleStep(0.001);
    cointPTauInv_->setToolTip(tr("P_τ⁻¹ of Koop, León-González and Strachan (2010): the prior precision of the cointegration space, a multiple of the identity."));
    cointRho_->setToolTip(tr("A time-varying cointegration space follows β_t = ρ β_{t-1} + η_t (Koop, León-González and Strachan 2011); ρ below one keeps it stationary."));
    coint->addRow(tr("Shrinkage v (constant space)"), cointShrinkage_);
    coint->addRow(tr("P_τ⁻¹ (× identity)"), cointPTauInv_);
    coint->addRow(tr("Autocorrelation ρ (time-varying space)"), cointRho_);
    layout->addWidget(cointBox_);

    stateBox_ = new QGroupBox(tr("Random walks of the coefficients"));
    auto *state = new QFormLayout(stateBox_);
    stateShape_ = number(3.0, 0.0, 1e6, 3);
    stateRate_ = small(1e-4);
    stateRateDet_ = small(0.01);
    omegaV_ = small(0.001);
    stateShape_->setToolTip(tr("The shape of the inverse gamma prior on each coefficient's random walk variance."));
    stateRate_->setToolTip(tr("The rate of that prior. A small rate lets the coefficients drift only slowly."));
    stateRateDet_->setToolTip(tr("The rate for the constant and the trend, which are usually let drift more."));
    omegaV_->setToolTip(tr("With non-centred random walks: the prior variance of ω, the signed standard deviation of each walk (Frühwirth-Schnatter and Wagner 2010)."));
    state->addRow(tr("Shape"), stateShape_);
    state->addRow(tr("Rate"), stateRate_);
    state->addRow(tr("Rate, deterministic terms"), stateRateDet_);
    state->addRow(tr("Prior variance of ω (non-centred)"), omegaV_);
    layout->addWidget(stateBox_);

    wishartBox_ = new QGroupBox(tr("Inverse Wishart"));
    auto *wishart = new QFormLayout(wishartBox_);
    dfExtra_ = new QSpinBox;
    dfExtra_->setRange(0, 1000);
    dfExtra_->setPrefix(tr("k + "));
    wishartScale_ = number(1.0, 1e-6, 1e6, 4);
    dfExtra_->setToolTip(tr("The degrees of freedom of the inverse Wishart prior on the error covariance, k plus this. More is a more informative prior."));
    wishartScale_->setToolTip(tr("The scale matrix of that prior, a multiple of the identity."));
    wishart->addRow(tr("Degrees of freedom"), dfExtra_);
    wishart->addRow(tr("Scale (× identity)"), wishartScale_);
    layout->addWidget(wishartBox_);

    // Lenza and Primiceri (2022): from a known period every shock's
    // covariance is s_t^2 Sigma, a few free scales and then a decay.
    breakBox_ = new QGroupBox(tr("Volatility break"));
    breakBox_->setObjectName(QStringLiteral("volatilityBreak"));
    auto *vb = new QFormLayout(breakBox_);
    break_ = new QComboBox;
    break_->setObjectName(QStringLiteral("breakPeriod"));
    break_->addItem(tr("None"));
    for (int r = 0; r < data_.length(); ++r)
    {
        break_->addItem(data_.periodLabel(r));
    }
    vb->addRow(tr("From"), break_);
    break_->setToolTip(tr("The first period whose shocks are scaled: March 2020 for monthly data through the pandemic. None for no break."));
    breakScales_ = new QSpinBox;
    breakScales_->setRange(1, 24);
    breakScales_->setValue(3);
    breakScales_->setToolTip(tr("Periods from the break with a scale of their own before the excess decays: "
                                "March, April and May 2020 in the paper."));
    vb->addRow(tr("Free scales"), breakScales_);
    breakShape_ = number(1.0, 1e-3, 1e3, 3);
    breakShape_->setToolTip(tr("Each free scale has a Pareto prior with scale one and this shape; the "
                               "paper's is 1."));
    vb->addRow(tr("Pareto shape of the scales"), breakShape_);
    rhoShape1_ = number(3.0357, 1e-3, 1e3, 4);
    rhoShape2_ = number(1.5089, 1e-3, 1e3, 4);
    rhoShape1_->setToolTip(tr("The first shape of the beta prior on ρ, the rate at which the excess volatility decays."));
    rhoShape2_->setToolTip(tr("The second shape of the beta prior on ρ. The defaults give a mode of 0.8 and a standard deviation of 0.2."));
    vb->addRow(tr("Beta on the decay ρ, shape 1"), rhoShape1_);
    vb->addRow(tr("shape 2"), rhoShape2_);
    vb->addRow(QString(), hint(tr("Lenza and Primiceri (2022): the outliers of a crisis -- March 2020 on -- "
                                  "scale the shocks' variance rather than move the coefficients. The "
                                  "defaults are the paper's: a beta with mode 0.8 and sd 0.2 on ρ. A "
                                  "constant-coefficient Wishart model only.")));
    connect(break_, &QComboBox::currentIndexChanged, this, &BuilderDialog::updateEnabled);
    layout->addWidget(breakBox_);

    gammaBox_ = new QGroupBox(tr("Gamma on the error precisions"));
    auto *gamma = new QFormLayout(gammaBox_);
    gammaShape_ = number(3.0, 0.0, 1e6, 3);
    gammaRate_ = small(0.01);
    gammaShape_->setToolTip(tr("The shape of the gamma prior on each error precision."));
    gammaRate_->setToolTip(tr("The rate of that prior: its mean precision is shape / rate."));
    gamma->addRow(tr("Shape"), gammaShape_);
    gamma->addRow(tr("Rate"), gammaRate_);
    layout->addWidget(gammaBox_);

    svBox_ = new QGroupBox(tr("Stochastic volatility"));
    auto *sv = new QFormLayout(svBox_);
    svShape_ = number(3.0, 0.0, 1e6, 3);
    svRate_ = small(0.03);
    svState_ = small(0.05);
    svShape_->setToolTip(tr("The shape of the inverse gamma prior on the variance of each log-volatility's innovation."));
    svRate_->setToolTip(tr("The rate of that prior: small values keep the volatilities smooth."));
    svState_->setToolTip(tr("Where the chain starts the variance of each log-volatility's innovation (bvartools' sigma_h)."));
    sv->addRow(tr("Shape of the volatility innovation"), svShape_);
    sv->addRow(tr("Rate"), svRate_);
    sv->addRow(tr("Initial innovation variance"), svState_);
    layout->addWidget(svBox_);
    return box;
}

QWidget *BuilderDialog::buildSamplerBox()
{
    auto *box = new QGroupBox(tr("Sampler and forecast"));
    auto *form = new QFormLayout(box);
    iterations_ = new QSpinBox;
    iterations_->setRange(1, 10000000);
    iterations_->setValue(20000);
    iterations_->setSingleStep(1000);
    burnin_ = new QSpinBox;
    burnin_->setRange(0, 10000000);
    burnin_->setValue(2000);
    burnin_->setSingleStep(500);
    thin_ = new QSpinBox;
    thin_->setRange(1, 1000);
    thin_->setToolTip(tr("Keep every n-th draw: it saves space, not precision."));
    burnin_->setToolTip(tr("Draws discarded at the start while the chain moves away from its starting values."));
    iterations_->setToolTip(tr("Draws kept after the burn-in, the size of every posterior summary."));
    form->addRow(tr("Draws kept"), iterations_);
    form->addRow(tr("Burn-in"), burnin_);
    form->addRow(tr("Thinning"), thin_);

    seed_ = new QLineEdit(QString::number(QRandomGenerator::global()->bounded(1, 2000000000)));
    seed_->setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("\\d{0,19}")), seed_));
    seed_->setPlaceholderText(tr("none: not reproducible"));
    seed_->setToolTip(tr("The random number seed. With one the same file gives the same draws on the same build; state it in a thesis."));
    form->addRow(tr("Seed"), seed_);

    horizon_ = new QSpinBox;
    horizon_->setRange(0, 200);
    horizon_->setValue(8);
    holdout_ = new QSpinBox;
    holdout_->setRange(0, 200);
    holdout_->setToolTip(tr("The last periods of the sample kept out of estimation and scored against the forecast of them."));
    horizon_->setToolTip(tr("Periods ahead to forecast; 0 writes no forecast."));
    form->addRow(tr("Forecast horizon"), horizon_);
    form->addRow(tr("Hold out for evaluation"), holdout_);
    form->addRow(QString(), hint(tr("Held-out periods are left out of estimation and scored against the "
                                     "forecast: the log predictive score of the Evaluation stage.")));
    exoForecast_ = hint(QString());
    form->addRow(QString(), exoForecast_);
    connect(holdout_, &QSpinBox::valueChanged, this, &BuilderDialog::updateEnabled);
    connect(horizon_, &QSpinBox::valueChanged, this, &BuilderDialog::updateEnabled);
    return box;
}

QWidget *BuilderDialog::buildSummary()
{
    auto *panel = new QWidget;
    panel->setObjectName(QStringLiteral("builderSummary"));
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(20, 8, 8, 8);
    auto *title = new QLabel(tr("Algorithm"));
    title->setObjectName(QStringLiteral("subtle"));
    algorithm_ = new QLabel;
    algorithm_->setObjectName(QStringLiteral("pageTitle"));
    algorithm_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    dimensions_ = new QLabel;
    dimensions_->setWordWrap(true);
    status_ = new QLabel;
    status_->setWordWrap(true);
    status_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    notes_ = new QLabel;
    notes_->setWordWrap(true);
    notes_->setObjectName(QStringLiteral("subtle"));
    notes_->setTextInteractionFlags(Qt::TextSelectableByMouse);

    layout->addWidget(title);
    layout->addWidget(algorithm_);
    layout->addSpacing(8);
    layout->addWidget(dimensions_);
    layout->addSpacing(12);
    auto *checkTitle = new QLabel(tr("What BayesTS says"));
    checkTitle->setObjectName(QStringLiteral("sectionTitle"));
    layout->addWidget(checkTitle);
    layout->addWidget(status_);
    layout->addWidget(notes_);
    layout->addStretch(1);
    return panel;
}

void BuilderDialog::updateEnabled()
{
    const bool tvp = coefTvp_->isChecked();
    const bool wishart = errWishart_->isChecked();
    const bool sv = errSv_->isChecked();
    int k = 0;
    for (int i = 0; i < variables_->count(); ++i)
    {
        k += variables_->item(i)->checkState() == Qt::Checked ? 1 : 0;
    }

    // The refusals of algorithms.md, as disabled controls.
    const bool vec = familyVec_->isChecked();
    int m = 0;
    for (int i = 0; i < exogenous_->count(); ++i)
    {
        m += exogenous_->item(i)->checkState() == Qt::Checked ? 1 : 0;
    }
    exoLags_->setEnabled(m > 0);
    exoLags_->setMinimum(vec ? 1 : 0);
    rank_->setEnabled(vec);
    rank_->setMaximum(std::max(1, k));
    restrictConstant_->setVisible(vec);
    restrictTrend_->setVisible(vec);
    restrictConstant_->setEnabled(vec && constant_->isChecked());
    restrictTrend_->setEnabled(vec && trend_->isChecked());
    cointBox_->setVisible(vec);
    cointShrinkage_->setEnabled(!tvp);
    cointPTauInv_->setEnabled(!tvp);
    cointRho_->setEnabled(tvp);
    nonCentred_->setEnabled(tvp);
    covar_->setEnabled(!wishart && k > 1 && !structural_->isChecked());
    if (!covar_->isEnabled())
    {
        covar_->setChecked(false);
    }
    structural_->setEnabled(!wishart && !covar_->isChecked() && k > 1);
    if (!structural_->isEnabled())
    {
        structural_->setChecked(false);
    }

    auto *model = qobject_cast<QStandardItemModel *>(selection_->model());
    const bool ssvsAllowed = !tvp && !sv;
    if (model != nullptr)
    {
        model->item(2)->setEnabled(ssvsAllowed);
    }
    if (!ssvsAllowed && selection_->currentIndex() == 2)
    {
        selection_->setCurrentIndex(1);
    }
    const bool selecting = selection_->currentIndex() > 0;
    inprior_->setEnabled(selecting);
    selectDet_->setEnabled(selecting);
    selectCovar_->setEnabled(selecting && covar_->isChecked());

    const bool minn = minnesota_->isChecked();
    randomWalk_->setEnabled(minn && !familyVec_->isChecked());
    coefPrecision_->setEnabled(!minn);
    detPrecision_->setEnabled(!minn);
    kappa1_->setEnabled(minn);
    kappa2_->setEnabled(minn);
    kappa4_->setEnabled(minn);

    stateBox_->setVisible(tvp);
    const bool nc = nonCentred_->isChecked();
    stateShape_->setEnabled(!nc);
    stateRate_->setEnabled(!nc);
    stateRateDet_->setEnabled(!nc);
    omegaV_->setEnabled(nc);
    wishartBox_->setVisible(wishart);
    // BayesTS offers the break on VarNormalWishart and VecNormalWishart only.
    breakBox_->setVisible(wishart);
    breakBox_->setEnabled(wishart && !tvp);
    {
        const bool on = break_->currentIndex() > 0;
        for (QWidget *w : std::initializer_list<QWidget *>{breakScales_, breakShape_, rhoShape1_, rhoShape2_})
        {
            w->setEnabled(on);
        }
    }
    gammaBox_->setVisible(errGamma_->isChecked());
    svBox_->setVisible(sv);

    // A structural model cannot be scored, so it has nothing to hold out for.
    // With exogenous variables a forecast is conditional on their values over
    // the horizon, which only the data after the sample -- or the periods held
    // out of it -- can supply; so the horizon is bounded by those, and the
    // holdout is what makes room for one.
    if (m > 0)
    {
        const QSignalBlocker b1(horizon_);
        const QSignalBlocker b2(holdout_);
        const int after = data_.length() - 1 - last_->currentIndex();
        holdout_->setMaximum(structural_->isChecked() ? 0 : 200);
        horizon_->setMaximum(std::max(0, after + holdout_->value()));
        exoForecast_->setText(tr("A forecast is conditional on the exogenous variables over the horizon: "
                                 "at most %1 period(s) here, the %2 after the sample and those held out.")
                                  .arg(horizon_->maximum())
                                  .arg(after));
        exoForecast_->setVisible(true);
    }
    else
    {
        const QSignalBlocker b1(horizon_);
        horizon_->setMaximum(200);
        holdout_->setMaximum(structural_->isChecked() ? 0 : horizon_->value());
        exoForecast_->setVisible(false);
    }
}

VarBuilderSpec BuilderDialog::readForm() const
{
    VarBuilderSpec s;
    s.data = data_;
    for (int i = 0; i < variables_->count(); ++i)
    {
        if (variables_->item(i)->checkState() == Qt::Checked)
        {
            s.columns.push_back(variables_->item(i)->data(Qt::UserRole).toInt());
        }
    }
    for (int i = 0; i < exogenous_->count(); ++i)
    {
        if (exogenous_->item(i)->checkState() == Qt::Checked)
        {
            s.exogenous.push_back(exogenous_->item(i)->data(Qt::UserRole).toInt());
        }
    }
    s.s = exoLags_->value();
    s.firstRow = first_->currentIndex();
    s.lastRow = last_->currentIndex();
    s.family = familyVec_->isChecked() ? VarBuilderSpec::Family::Vec : VarBuilderSpec::Family::Var;
    s.rank = rank_->value();
    s.restrictConstant = restrictConstant_->isChecked();
    s.restrictTrend = restrictTrend_->isChecked();
    s.cointShrinkage = cointShrinkage_->value();
    s.cointPTauInv = cointPTauInv_->value();
    s.cointRho = cointRho_->value();
    s.p = p_->value();
    s.constant = constant_->isChecked();
    s.trend = trend_->isChecked();
    s.timeVarying = coefTvp_->isChecked();
    s.nonCentred = s.timeVarying && nonCentred_->isChecked();
    s.errors = errWishart_->isChecked() ? VarBuilderSpec::Errors::Wishart
               : errGamma_->isChecked() ? VarBuilderSpec::Errors::Gamma
                                        : VarBuilderSpec::Errors::Stochvol;
    s.covar = covar_->isChecked();
    s.structural = structural_->isChecked();
    s.selection = static_cast<VarBuilderSpec::Selection>(selection_->currentIndex());
    s.inclusionPrior = inprior_->value();
    s.selectDeterministic = selectDet_->isChecked();
    s.selectCovariances = selectCovar_->isChecked();

    s.minnesota = minnesota_->isChecked();
    s.randomWalkMean = s.minnesota && randomWalk_->isChecked();
    s.coefPrecision = coefPrecision_->value();
    s.detPrecision = detPrecision_->value();
    s.kappa1 = kappa1_->value();
    s.kappa2 = kappa2_->value();
    s.kappa4 = kappa4_->value();
    s.stateShape = stateShape_->value();
    s.stateRate = valueOf(stateRate_);
    s.stateRateDet = valueOf(stateRateDet_);
    s.omegaV = valueOf(omegaV_);
    s.wishartDfExtra = dfExtra_->value();
    s.wishartScale = wishartScale_->value();
    if (errWishart_->isChecked() && !coefTvp_->isChecked() && break_->currentIndex() > 0)
    {
        s.volatilityBreak = break_->currentIndex() - 1;
        s.volatilityScales = breakScales_->value();
        s.volatilityShape = breakShape_->value();
        s.rhoShape1 = rhoShape1_->value();
        s.rhoShape2 = rhoShape2_->value();
    }
    s.gammaShape = gammaShape_->value();
    s.gammaRate = valueOf(gammaRate_);
    s.svShape = svShape_->value();
    s.svRate = valueOf(svRate_);
    s.svStateVariance = valueOf(svState_);

    s.iterations = iterations_->value();
    s.burnin = burnin_->value();
    s.thin = thin_->value();
    if (!seed_->text().isEmpty())
    {
        s.seed = seed_->text().toULongLong();
    }
    s.horizon = horizon_->value();
    s.holdout = holdout_->value();
    return s;
}

void BuilderDialog::scheduleCheck()
{
    debounce_->start();
}

void BuilderDialog::runCheck()
{
    updateEnabled();
    const VarBuilderSpec spec = readForm();
    const VarBuilderSummary summary = summarise(spec);
    algorithm_->setText(spec.algorithm());
    dimensions_->setText(tr("%1 variables · %2 periods · %3 regressors per equation · %4 coefficients%5%6")
                             .arg(summary.k)
                             .arg(summary.periods)
                             .arg(summary.nx)
                             .arg(summary.nparams)
                             .arg(summary.npsi > 0 ? tr(" · %1 covariances").arg(summary.npsi) : QString())
                             .arg(spec.vec() ? tr(" · rank %1, %2 rows of β").arg(spec.rank).arg(summary.kBeta)
                                             : QString()));

    QString status;
    bool ok = false;
    if (!summary.refusal.isEmpty())
    {
        status = tr("✗  %1").arg(summary.refusal);
    }
    else
    {
        const QString file = scratch_.filePath(QStringLiteral("draft.h5"));
        try
        {
            writeVarModel(spec, file);
            const CheckReport report = ModelRunner::check(file, QString());
            ok = report.ok;
            status = ok ? tr("✓  BayesTS accepts this model.")
                        : tr("✗  BayesTS refuses it: %1").arg(report.error);
            QStringList notes = summary.notes;
            notes << report.warnings;
            notes_->setText(notes.join(QStringLiteral("\n\n")));
        }
        catch (const std::exception &e)
        {
            status = tr("✗  %1").arg(QString::fromUtf8(e.what()));
        }
    }
    if (!summary.refusal.isEmpty())
    {
        notes_->clear();
    }
    status_->setText(status);
    status_->setStyleSheet(ok ? QStringLiteral("color: #1E7B4A; font-weight: 600;")
                              : QStringLiteral("color: #B3261E; font-weight: 600;"));
    createButton_->setEnabled(ok);
}

QString BuilderDialog::suggestedName() const
{
    const VarBuilderSpec s = readForm();
    QString name = QStringLiteral("%1-p%2").arg(s.algorithm()).arg(s.p);
    if (s.vec())
    {
        name += QStringLiteral("-r%1").arg(s.rank);
    }
    if (s.covar)
    {
        name += QStringLiteral("-covar");
    }
    if (s.structural)
    {
        name += QStringLiteral("-structural");
    }
    if (s.selection == VarBuilderSpec::Selection::Bvs)
    {
        name += QStringLiteral("-bvs");
    }
    if (s.selection == VarBuilderSpec::Selection::Ssvs)
    {
        name += QStringLiteral("-ssvs");
    }
    if (s.nonCentred)
    {
        name += QStringLiteral("-noncentred");
    }
    return name + QStringLiteral(".h5");
}

void BuilderDialog::create()
{
    const QString dir = projectDir_.isEmpty() ? QFileInfo(data_.source).absolutePath() : projectDir_;
    const QString path = QFileDialog::getSaveFileName(this, tr("Save Model File"),
                                                      QDir(dir).filePath(suggestedName()),
                                                      tr("BayesTS model files (*.h5)"));
    if (!path.isEmpty())
    {
        createAt(path);
    }
}

bool BuilderDialog::createAt(const QString &path)
{
    try
    {
        writeVarModel(readForm(), path);
    }
    catch (const std::exception &e)
    {
        QMessageBox::warning(this, tr("Cannot write the model"), QString::fromUtf8(e.what()));
        return false;
    }
    created_ = path;
    accept();
    return true;
}

} // namespace studio
