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
    double coefPrecision = 1.0;       ///< v_i
    double detPrecision = 0.1;        ///< v_i_det
    double kappa1 = 0.2, kappa2 = 0.5, kappa4 = 100.0; ///< Minnesota: overall, cross, deterministic
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
    double gammaShape = 3.0, gammaRate = 0.01;
    double svMu = 0.0, svPrecision = 1.0, svShape = 3.0, svRate = 0.03, svStateVariance = 0.05,
           svOffset = 1e-4, svOmegaV = 0.1;

    // Sampler
    int iterations = 20000, burnin = 2000, thin = 1;
    std::optional<std::uint64_t> seed;

    // Forecast
    int horizon = 0;
    int holdout = 0;                  ///< last periods kept out of estimation, written as /data/test/y

    bool vec() const { return family == Family::Vec; }
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
