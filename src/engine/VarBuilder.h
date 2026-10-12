#pragma once

#include <QString>
#include <QStringList>

#include <cstdint>
#include <optional>

#include "engine/DataSet.h"

namespace studio
{

/// Everything the model builder asks for, for the twelve Gaussian VARs and VECs
/// of the taxonomy: {VAR, VEC} x {constant, time-varying} coefficients x
/// {Wishart, gamma, stochastic volatility} errors. Defaults are those of the
/// bvartools article's replication script, so a model built here and one built
/// there agree.
struct VarBuilderSpec
{
    enum class Family { Var, Vec } family = Family::Var;

    // Data: the columns of the data set that are endogenous, in the order the
    // model takes them (which matters for a structural or covariance block),
    // and the rows used, inclusive. A VEC takes them as levels.
    DataSet data;
    std::vector<int> columns;
    /// Columns that are weakly exogenous: regressors the model does not
    /// explain, entering at lags 0 to `s` -- in levels for a VAR, as
    /// differences (and in the cointegration space at lag 1) for a VEC.
    std::vector<int> exogenous;
    int s = 1;          ///< lag order of the exogenous variables; for a VEC a level order, at least 1
    int firstRow = 0;
    int lastRow = -1;   ///< -1: the last row

    // Specification. `p` is a level order for a VEC too, so a VEC of order 1
    // has no lagged differences.
    int p = 2;
    bool constant = true;
    bool trend = false;
    bool restrictConstant = false;    ///< VEC: the constant inside the cointegration space
    bool restrictTrend = false;       ///< VEC: the trend inside the cointegration space
    int rank = 1;                     ///< VEC: cointegration rank
    bool timeVarying = false;
    /// The first `nIid` endogenous variables carry no coefficients at all --
    /// no lags, no deterministic terms: white noise reaching the others only
    /// through the error covariance. High-frequency surprises as variables of
    /// a monthly VAR, Jarociński and Karadi (2020); BayesTS's /model/n_iid,
    /// for the constant-coefficient VARs, without a structural form or
    /// variable selection.
    int nIid = 0;
    enum class Errors { Wishart, Gamma, Stochvol } errors = Errors::Wishart;
    bool covar = false;       ///< the covariance block, gamma or SV only
    bool structural = false;  ///< contemporaneous coefficients, gamma or SV without covar
    enum class Selection { None, Bvs, Ssvs } selection = Selection::None;
    bool selectDeterministic = false; ///< whether selection reaches the deterministic terms
    bool selectCovariances = false;   ///< whether selection reaches the covariance block
    double inclusionPrior = 0.5;
    double ssvsTau0 = 0.1, ssvsTau1 = 10.0;  ///< factors on the OLS standard errors (semiautomatic)

    // Coefficient prior
    bool minnesota = false;
    /// Minnesota: centre each variable's own first lag on one, a random walk,
    /// for series in levels (Litterman 1986); zero, white noise, otherwise.
    /// A VAR's only -- a VEC's differences need no centring.
    bool randomWalkMean = false;
    double coefPrecision = 1.0;       ///< v_i
    double detPrecision = 0.1;        ///< v_i_det
    double kappa1 = 0.2, kappa2 = 0.5, kappa4 = 100.0; ///< Minnesota: overall, cross, deterministic
    /// Minnesota: the prior standard deviation of lag l falls as l^-lagDecay,
    /// its variance as l^-2 lagDecay -- 1 is bvartools' minnesota_prior(),
    /// Pinchetti (2026) takes 2.
    double lagDecay = 1.0;
    /// Minnesota: where the scales sigma_i come from -- the VAR's least
    /// squares residuals, bvartools' rule, or a univariate AR(p) with a
    /// constant per variable, Litterman's and most replication codes'.
    enum class MinnesotaScale { VarResiduals, Autoregressions } minnesotaScale = MinnesotaScale::VarResiduals;
    /// Minnesota, a VAR: each variable's own first lag's prior mean, in the
    /// order of `columns`; empty for `randomWalkMean`'s all-or-nothing rule.
    std::vector<double> ownLagMean;
    /// Minnesota: the prior variance of every deterministic term, when
    /// positive; else kappa1 kappa4 sigma_i^2.
    double minnesotaDetVariance = 0.0;
    double covarPrecision = 1.0;      ///< psi v_i

    // Cointegration space (VEC). Constant: Koop, León-González and Strachan
    // (2010), v and P_tau^-1 = this times I. Time-varying: the state equation
    // beta_t = rho beta_{t-1} + eta_t and its stationary distribution.
    double cointShrinkage = 0.1;      ///< coint v_i
    double cointPTauInv = 1.0;        ///< coint p_tau_i
    double cointRho = 0.999;          ///< coint rho

    // Random walks (time-varying coefficients)
    bool nonCentred = false;          ///< Frühwirth-Schnatter and Wagner (2010), for the time-variation test
    double stateShape = 3.0, stateRate = 1e-4, stateRateDet = 0.01;
    double omegaV = 0.001;

    // Errors
    int wishartDfExtra = 0;           ///< df = k + this
    double wishartScale = 1.0;
    /// The Wishart's scale diag(sigma_i^2) from the Minnesota scales' source
    /// rather than wishartScale times I.
    bool wishartScaleFromResiduals = false;
    double gammaShape = 3.0, gammaRate = 0.01;
    double svMu = 0.0, svPrecision = 1.0, svShape = 3.0, svRate = 0.03, svStateVariance = 0.05,
           svOffset = 1e-4, svOmegaV = 0.1;

    // Volatility break (Lenza and Primiceri 2022), VarNormalWishart and
    // VecNormalWishart only: from this data-set row every shock's covariance
    // is s_t^2 Sigma, `volatilityScales` free scales and then a decay at rate
    // rho. -1: none. A break after the estimation sample is no break, and the
    // file is written without one.
    int volatilityBreak = -1;
    int volatilityScales = 3;
    double volatilityShape = 1.0;                         ///< Pareto shape of each free scale, the paper's
    double rhoShape1 = 3.0357, rhoShape2 = 1.5089;        ///< beta on rho: the paper's mode 0.8, sd 0.2

    // Sampler
    int iterations = 20000, burnin = 2000, thin = 1;
    std::optional<std::uint64_t> seed;

    // Forecast
    int horizon = 0;
    int holdout = 0;                  ///< last periods kept out of estimation, written as /data/test/y

    bool vec() const { return family == Family::Vec; }
    /// Whether the volatility break lies inside the estimation sample, and is
    /// written: false for none and for one after the sample.
    bool breakInSample() const;
    QString algorithm() const;
};

/// What the builder can say before writing: dimensions, and the combinations
/// BayesTS refuses, so the form can disable them. A non-empty `refusal` means
/// the file would not run.
struct VarBuilderSummary
{
    int k = 0;
    int periods = 0;   ///< tt, after lags and holdout
    int nx = 0;        ///< regressors per equation, after a VEC's loadings
    int nparams = 0;   ///< coefficients
    int npsi = 0;
    int kBeta = 0;     ///< VEC: rows of the cointegration matrix
    int m = 0;         ///< exogenous variables
    QString refusal;
    QStringList notes;
};

VarBuilderSummary summarise(const VarBuilderSpec &spec);

/// Writes the model file. Throws std::runtime_error when the spec is refused
/// or the data cannot support it.
void writeVarModel(const VarBuilderSpec &spec, const QString &path);

} // namespace studio
