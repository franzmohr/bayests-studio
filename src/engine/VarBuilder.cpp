#include "engine/VarBuilder.h"

#include <QObject>

#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

#include "io/hdf5/hdf5_and_armadillo.h"

namespace studio
{

namespace
{

using Errors = VarBuilderSpec::Errors;
using Selection = VarBuilderSpec::Selection;

// -- Writing, through BayesTS's own writer so the orientation is the one its
// readers expect: an Armadillo matrix lands transposed in the dataspace, and a
// vector as a single row (see make_model_fixture.cpp, which this mirrors).

void ensureGroup(const ModelFile &file, const std::string &path)
{
    std::size_t start = 1;
    while (start <= path.size())
    {
        const std::size_t slash = path.find('/', start);
        const std::string partial = path.substr(0, slash == std::string::npos ? path.size() : slash);
        if (!file.exist(partial))
        {
            file.createGroup(partial);
        }
        if (slash == std::string::npos)
        {
            break;
        }
        start = slash + 1;
    }
}

void writeMat(const ModelFile &file, const std::string &path, const arma::mat &m)
{
    ensureGroup(file, path.substr(0, path.rfind('/')));
    write_armadillo_matrix_to_hdf5(file, path, m, false);
}

void writeRow(const ModelFile &file, const std::string &path, const arma::vec &v)
{
    writeMat(file, path, arma::mat(v));
}

template <typename T>
void writeScalar(const ModelFile &file, const std::string &path, T value)
{
    ensureGroup(file, path.substr(0, path.rfind('/')));
    file.createDataSet<T>(path, HighFive::DataSpace::From(value)).write(value);
}

void writeIntRow(const ModelFile &file, const std::string &path, const std::vector<int> &values)
{
    ensureGroup(file, path.substr(0, path.rfind('/')));
    const std::vector<std::vector<int>> data{values};
    file.createDataSet<int>(path, HighFive::DataSpace::From(data)).write(data);
}

template <typename T>
void writeAttribute(const ModelFile &file, const std::string &group, const std::string &name, const T &value)
{
    ensureGroup(file, group);
    HighFive::Group g = file.getGroup(group);
    g.createAttribute<T>(name, HighFive::DataSpace::From(value)).write(value);
}

template <typename T>
void writeDatasetAttribute(const ModelFile &file, const std::string &path, const std::string &name, const T &value)
{
    HighFive::DataSet ds = file.getDataSet(path);
    ds.createAttribute(name, value);   // the element type and dataspace from the value
}

/// A time series the way bayests-r writes one (write_bayests_tree()): one
/// row per variable and one column per period in the dataspace, `variables`
/// and `tsp` as attributes, so that R reads it back as a named `ts` with its
/// dates. BayesTS reads this layout of /data/train/y exactly as it reads the
/// stacked one.
void writeSeries(const ModelFile &file, const std::string &path, const arma::mat &periodsByVariables,
                 const std::vector<std::string> &variables, const TimeIndex &index)
{
    writeMat(file, path, periodsByVariables);   // (periods, k): (k, periods) on disk
    writeDatasetAttribute(file, path, "variables", variables);
    writeDatasetAttribute(file, path, "tsp", std::vector<double>{index.start, index.end, index.frequency});
}

// -- The model's arithmetic.

struct Layout
{
    bool vec = false;
    int k = 0;
    int p = 0;          // level order
    int lagBlocks = 0;  // p for a VAR, p - 1 for a VEC
    int n = 0;          // unrestricted deterministic terms
    int nRestricted = 0;
    int kBeta = 0;
    int rank = 0;
    int nAlpha = 0;
    int nx = 0;         // regressors per equation after the loadings
    int nNonStruct = 0;
    int nStruct = 0;
    int nparams = 0;
    int npsi = 0;
    int estFirst = 0;   // first estimation row of the data set
    int estLast = 0;    // last estimation row
    int tt = 0;
    int testRows = 0;

    // Deterministic terms, in the order they are written.
    bool unrestrictedConstant = false, unrestrictedTrend = false;
    bool restrictedConstant = false, restrictedTrend = false;
};

Layout layoutOf(const VarBuilderSpec &s)
{
    Layout l;
    l.vec = s.vec();
    l.k = static_cast<int>(s.columns.size());
    l.p = s.p;
    l.lagBlocks = l.vec ? std::max(s.p - 1, 0) : s.p;
    l.restrictedConstant = l.vec && s.constant && s.restrictConstant;
    l.restrictedTrend = l.vec && s.trend && s.restrictTrend;
    l.unrestrictedConstant = s.constant && !l.restrictedConstant;
    l.unrestrictedTrend = s.trend && !l.restrictedTrend;
    l.n = (l.unrestrictedConstant ? 1 : 0) + (l.unrestrictedTrend ? 1 : 0);
    l.nRestricted = (l.restrictedConstant ? 1 : 0) + (l.restrictedTrend ? 1 : 0);
    l.rank = l.vec ? s.rank : 0;
    l.kBeta = l.vec ? l.k + l.nRestricted : 0;
    l.nAlpha = l.k * l.rank;
    l.nx = l.k * l.lagBlocks + l.n;
    l.nNonStruct = l.nAlpha + l.k * l.nx;
    l.nStruct = s.structural ? l.k * (l.k - 1) / 2 : 0;
    l.nparams = l.nNonStruct + l.nStruct;
    l.npsi = s.covar ? l.k * (l.k - 1) / 2 : 0;
    const int last = s.lastRow < 0 ? s.data.length() - 1 : s.lastRow;
    l.testRows = std::min(s.holdout, s.horizon);
    l.estFirst = s.firstRow + s.p;
    l.estLast = last - l.testRows;
    l.tt = l.estLast - l.estFirst + 1;
    return l;
}

/// The endogenous data, data-set rows by model variables, in levels.
arma::mat endogenous(const VarBuilderSpec &s)
{
    arma::mat y(s.data.values.n_rows, s.columns.size());
    for (std::size_t j = 0; j < s.columns.size(); ++j)
    {
        y.col(j) = s.data.values.col(static_cast<arma::uword>(s.columns[j]));
    }
    return y;
}

/// The regressors of one period after a VEC's loadings: the lag blocks -- of
/// levels for a VAR, of differences for a VEC -- then the unrestricted
/// deterministic terms. `trendValue` counts from 1 at the first estimation
/// period.
arma::vec regressors(const arma::mat &y, const Layout &l, int row, double trendValue)
{
    arma::vec x(static_cast<arma::uword>(l.nx));
    const auto k = static_cast<arma::uword>(l.k);
    for (int lag = 1; lag <= l.lagBlocks; ++lag)
    {
        const auto first = static_cast<arma::uword>((lag - 1) * l.k);
        const auto r = static_cast<arma::uword>(row - lag);
        x.subvec(first, first + k - 1) = l.vec ? arma::vec(arma::trans(y.row(r) - y.row(r - 1)))
                                               : arma::vec(arma::trans(y.row(r)));
    }
    arma::uword d = k * static_cast<arma::uword>(l.lagBlocks);
    if (l.unrestrictedConstant)
    {
        x(d++) = 1.0;
    }
    if (l.unrestrictedTrend)
    {
        x(d++) = trendValue;
    }
    return x;
}

/// The error correction term of one period: y_{t-1}, then the restricted terms.
arma::vec errorCorrection(const arma::mat &y, const Layout &l, int row, double trendValue)
{
    arma::vec w(static_cast<arma::uword>(l.kBeta));
    const auto k = static_cast<arma::uword>(l.k);
    w.subvec(0, k - 1) = arma::trans(y.row(static_cast<arma::uword>(row - 1)));
    arma::uword d = k;
    if (l.restrictedConstant)
    {
        w(d++) = 1.0;
    }
    if (l.restrictedTrend)
    {
        w(d++) = trendValue;
    }
    return w;
}

/// The symmetric inverse square root of a positive definite matrix.
arma::mat inverseRoot(const arma::mat &m)
{
    arma::vec values;
    arma::mat vectors;
    arma::eig_sym(values, vectors, arma::symmatu(m));
    return vectors * arma::diagmat(1.0 / arma::sqrt(arma::clamp(values, 1e-12, arma::datum::inf))) * vectors.t();
}

/// Johansen's maximum likelihood estimate of beta (k_beta x rank): the reduced
/// rank regression of the differences on the error correction term, both
/// partialled on the other regressors -- bvartools' .coint_ml().
arma::mat johansenBeta(const arma::mat &dy, const arma::mat &w, const arma::mat &x, int rank)
{
    arma::mat r0 = dy;   // tt x k
    arma::mat r1 = w;    // tt x k_beta
    if (x.n_cols > 0)
    {
        const arma::mat projection = x * arma::pinv(x.t() * x) * x.t();
        r0 -= projection * dy;
        r1 -= projection * w;
    }
    const double tt = static_cast<double>(dy.n_rows);
    const arma::mat s00 = r0.t() * r0 / tt;
    const arma::mat s01 = r0.t() * r1 / tt;
    const arma::mat s11 = r1.t() * r1 / tt;
    const arma::mat s11Root = inverseRoot(s11);
    arma::mat problem = s11Root * s01.t() * arma::pinv(s00) * s01 * s11Root;
    problem = (problem + problem.t()) / 2.0;
    arma::vec values;
    arma::mat vectors;
    arma::eig_sym(values, vectors, problem);   // ascending
    const auto r = static_cast<arma::uword>(rank);
    return s11Root * arma::fliplr(vectors.tail_cols(r));
}

} // namespace

QString VarBuilderSpec::algorithm() const
{
    QString name = vec() ? QStringLiteral("Vec") : QStringLiteral("Var");
    name += timeVarying ? QStringLiteral("Tvp") : QStringLiteral("Normal");
    switch (errors)
    {
    case Errors::Wishart: name += QStringLiteral("Wishart"); break;
    case Errors::Gamma: name += QStringLiteral("Gamma"); break;
    case Errors::Stochvol: name += QStringLiteral("Stochvol"); break;
    }
    return name;
}

VarBuilderSummary summarise(const VarBuilderSpec &s)
{
    VarBuilderSummary out;
    const Layout l = layoutOf(s);
    out.k = l.k;
    out.periods = l.tt;
    out.nx = l.nx;
    out.nparams = l.nparams;
    out.npsi = l.npsi;
    out.kBeta = l.kBeta;

    // The refusals BayesTS makes, in its own terms, so the form can stop them
    // before a file is written. `bayests check` remains the authority: the
    // builder runs it over every file it writes.
    if (s.data.empty())
    {
        out.refusal = QObject::tr("Import a data set first.");
    }
    else if (l.k < 1)
    {
        out.refusal = QObject::tr("Select at least one endogenous variable.");
    }
    else if (s.p < 1)
    {
        out.refusal = QObject::tr("The lag order must be at least 1.");
    }
    else if (l.vec && l.k < 2)
    {
        out.refusal = QObject::tr("A VEC needs two variables or more.");
    }
    else if (l.vec && (s.rank < 1 || s.rank > l.k))
    {
        out.refusal = QObject::tr("The cointegration rank must lie between 1 and %1.").arg(l.k);
    }
    else if (l.vec && s.timeVarying && (s.cointRho <= 0.0 || s.cointRho > 1.0))
    {
        out.refusal = QObject::tr("The autocorrelation ρ of the cointegration space must lie in (0, 1].");
    }
    else if (l.vec && s.timeVarying && s.cointRho >= 1.0)
    {
        // At rho = 1 the state before the sample has no stationary
        // distribution to give it a prior.
        out.refusal = QObject::tr("ρ = 1 leaves the cointegration space without a stationary prior; "
                                  "use a value just below one.");
    }
    else if (s.covar && s.errors == Errors::Wishart)
    {
        out.refusal = QObject::tr("A Wishart precision has no separate covariance block.");
    }
    else if (s.covar && l.k < 2)
    {
        out.refusal = QObject::tr("A covariance block needs two variables or more.");
    }
    else if (s.structural && (s.errors == Errors::Wishart || s.covar))
    {
        out.refusal = QObject::tr("A structural form needs a diagonal error covariance: gamma or "
                                  "stochastic volatility errors without the covariance block.");
    }
    else if (s.structural && l.k < 2)
    {
        out.refusal = QObject::tr("A structural form needs two variables or more.");
    }
    else if (s.structural && s.horizon > 0 && s.holdout > 0)
    {
        // A structural model's regressors include the contemporaneous
        // observations and its density the Jacobian of A_0, so BayesTS refuses
        // to score its forecast against held-out data.
        out.refusal = QObject::tr("A structural model cannot be scored against held-out periods; "
                                  "set the holdout to 0.");
    }
    else if (s.selection == Selection::Ssvs && (s.timeVarying || s.errors == Errors::Stochvol))
    {
        out.refusal = QObject::tr("SSVS is not available with time-varying coefficients or "
                                  "stochastic volatility; use BVS.");
    }
    else if (l.tt < 2)
    {
        out.refusal = QObject::tr("The sample is too short for %1 lags.").arg(s.p);
    }
    else if (s.iterations < 1 || s.burnin < 0 || s.thin < 1)
    {
        out.refusal = QObject::tr("Draws must be positive, the burn-in non-negative, the thinning at least 1.");
    }
    else if (s.seed && *s.seed > 2147483647ULL)
    {
        out.refusal = QObject::tr("The seed must be at most 2147483647, the largest integer R stores.");
    }

    if (out.refusal.isEmpty())
    {
        if (l.tt * l.k <= l.nparams)
        {
            out.notes << QObject::tr("Fewer observations than coefficients: least squares starting "
                                     "values are not available, the chain starts at zero.");
        }
        if (s.selection == Selection::Bvs && !s.minnesota && 1.0 / s.coefPrecision > 100.0)
        {
            out.notes << QObject::tr("A prior variance above 100 makes BVS inclusion probabilities "
                                     "describe the prior (Korobilis 2013).");
        }
        if (s.horizon > 0 && s.holdout > 0)
        {
            out.notes << QObject::tr("The last %1 periods are held out and scored against the forecast.")
                             .arg(l.testRows);
        }
        if (l.vec)
        {
            out.notes << QObject::tr("The model is estimated in differences and forecast in levels; "
                                     "selection never reaches the %1 loadings.").arg(l.nAlpha);
        }
    }
    return out;
}

void writeVarModel(const VarBuilderSpec &s, const QString &path)
{
    const VarBuilderSummary summary = summarise(s);
    if (!summary.refusal.isEmpty())
    {
        throw std::runtime_error(summary.refusal.toStdString());
    }
    const Layout l = layoutOf(s);
    const auto k = static_cast<arma::uword>(l.k);
    const auto tt = static_cast<arma::uword>(l.tt);
    const auto nparams = static_cast<arma::uword>(l.nparams);
    const auto npsi = static_cast<arma::uword>(l.npsi);
    const auto nAlpha = static_cast<arma::uword>(l.nAlpha);
    const auto nBeta = static_cast<arma::uword>(l.kBeta * l.rank);
    const arma::mat y = endogenous(s);   // levels

    std::vector<std::string> vars;
    for (const int c : s.columns)
    {
        vars.push_back(s.data.names[c].toStdString());
    }

    // -- The response, the regressors after the loadings, and a VEC's error
    // correction term, one row per estimation period.
    arma::mat response(tt, k);
    arma::mat x(tt, static_cast<arma::uword>(l.nx));
    arma::mat w(tt, static_cast<arma::uword>(l.kBeta));
    for (arma::uword t = 0; t < tt; ++t)
    {
        const int row = l.estFirst + static_cast<int>(t);
        const auto r = static_cast<arma::uword>(row);
        response.row(t) = l.vec ? arma::rowvec(y.row(r) - y.row(r - 1)) : arma::rowvec(y.row(r));
        x.row(t) = arma::trans(regressors(y, l, row, static_cast<double>(t + 1)));
        if (l.vec)
        {
            w.row(t) = arma::trans(errorCorrection(y, l, row, static_cast<double>(t + 1)));
        }
    }
    const arma::vec yStacked = arma::vectorise(response.t());   // by period

    // -- Where a VEC's chain starts in the space: Johansen's estimate, on the
    // scale of the state equation for a time-varying one (bvartools'
    // .tvp_initial_beta_scale()), so the loadings fitted against it match.
    arma::mat beta;
    if (l.vec)
    {
        beta = johansenBeta(response, w, x, l.rank);
        if (s.timeVarying)
        {
            const double target = std::sqrt(static_cast<double>(l.kBeta) / (1.0 - s.cointRho * s.cointRho));
            for (arma::uword c = 0; c < beta.n_cols; ++c)
            {
                beta.col(c) *= target / arma::norm(beta.col(c));
            }
        }
    }

    // -- z, stacked by period: per period kron((beta' w_t)', I_k) for a VEC's
    // loadings (which every sampler rebuilds from its own draw), a
    // kron(x_t', I_k) block and, for a structural model, -y_t of every
    // variable in the equations after it -- the columns bvartools builds.
    arma::mat z(tt * k, nparams, arma::fill::zeros);
    const arma::mat eye = arma::eye<arma::mat>(k, k);
    for (arma::uword t = 0; t < tt; ++t)
    {
        if (l.vec)
        {
            z.submat(t * k, 0, (t + 1) * k - 1, nAlpha - 1) =
                arma::kron(arma::trans(beta.t() * w.row(t).t()), eye);
        }
        if (l.nx > 0)
        {
            z.submat(t * k, nAlpha, (t + 1) * k - 1, static_cast<arma::uword>(l.nNonStruct) - 1) =
                arma::kron(x.row(t), eye);
        }
        arma::uword col = static_cast<arma::uword>(l.nNonStruct);
        for (arma::uword j = 0; j + 1 < k && s.structural; ++j)
        {
            for (arma::uword i = j + 1; i < k; ++i)
            {
                z(t * k + i, col++) = -response(t, j);
            }
        }
    }

    // -- A name per coefficient, for the views: "income <- invest(-1)", and
    // for a VEC "D income <- ec1".
    const std::string arrow = " ← ";
    const std::string delta = l.vec ? "Δ" : "";
    std::vector<std::string> names;
    std::vector<std::string> terms;   // the rows of a VEC's beta
    for (int r = 0; r < l.rank; ++r)
    {
        for (const auto &eq : vars)
        {
            names.push_back(delta + eq + arrow + "ec" + std::to_string(r + 1));
        }
    }
    {
        std::vector<std::string> regressor;
        for (int lag = 1; lag <= l.lagBlocks; ++lag)
        {
            for (const auto &v : vars)
            {
                regressor.push_back(delta + v + "(-" + std::to_string(lag) + ")");
            }
        }
        if (l.unrestrictedConstant)
        {
            regressor.push_back("constant");
        }
        if (l.unrestrictedTrend)
        {
            regressor.push_back("trend");
        }
        for (const auto &r : regressor)
        {
            for (const auto &eq : vars)
            {
                names.push_back(delta + eq + arrow + r);
            }
        }
        for (arma::uword j = 0; j + 1 < k && s.structural; ++j)
        {
            for (arma::uword i = j + 1; i < k; ++i)
            {
                names.push_back(delta + vars[i] + arrow + delta + vars[j] + " (contemporaneous)");
            }
        }
        if (l.vec)
        {
            for (const auto &v : vars)
            {
                terms.push_back(v + "(-1)");
            }
            if (l.restrictedConstant)
            {
                terms.push_back("constant");
            }
            if (l.restrictedTrend)
            {
                terms.push_back("trend");
            }
        }
    }

    // -- Least squares given the starting beta, for the starting values and
    // the Minnesota scales.
    arma::vec ols(nparams, arma::fill::zeros);
    arma::vec olsSe(nparams, arma::fill::ones);
    bool haveOls = false;
    arma::mat ztzInv;
    if (tt * k > nparams && arma::inv_sympd(ztzInv, arma::mat(z.t() * z)))
    {
        ols = ztzInv * (z.t() * yStacked);
        haveOls = true;
    }
    arma::mat u = arma::reshape(yStacked - z * ols, k, tt);   // k x tt
    arma::vec residVar(k);
    for (arma::uword i = 0; i < k; ++i)
    {
        residVar(i) = std::max(arma::var(u.row(i).t()), 1e-8);
    }
    if (haveOls)
    {
        // Equation-wise standard errors, for SSVS's semiautomatic tau.
        for (arma::uword q = 0; q < nparams; ++q)
        {
            olsSe(q) = std::sqrt(std::max(ztzInv(q, q) * residVar(q % k), 1e-12));
        }
    }

    // -- The prior precision of every coefficient. After the loadings,
    // coefficient nAlpha + q*k + i is regressor q in equation i, which is what
    // kron(x', I_k) lays out.
    arma::vec precision(nparams, arma::fill::value(s.coefPrecision));
    arma::uvec isDet(nparams, arma::fill::zeros);
    for (arma::uword q = 0; q < static_cast<arma::uword>(l.nx); ++q)
    {
        const bool det = q >= static_cast<arma::uword>(l.k * l.lagBlocks);
        for (arma::uword i = 0; i < k; ++i)
        {
            const arma::uword pos = nAlpha + q * k + i;
            isDet(pos) = det ? 1 : 0;
            if (!s.minnesota)
            {
                precision(pos) = det ? s.detPrecision : s.coefPrecision;
                continue;
            }
            // Appendix A of the article: kappa1/l^2 own lags, kappa1 kappa2/l^2
            // sigma_i^2/sigma_j^2 other lags, kappa1 kappa4 sigma_i^2 deterministic.
            double variance = 0.0;
            if (det)
            {
                variance = s.kappa1 * s.kappa4 * residVar(i);
            }
            else
            {
                const double lag = static_cast<double>(q / k + 1);
                const arma::uword j = q % k;
                variance = (i == j) ? s.kappa1 / (lag * lag)
                                    : s.kappa1 * s.kappa2 / (lag * lag) * residVar(i) / residVar(j);
            }
            precision(pos) = 1.0 / variance;
        }
    }

    // -- Starting values of the covariance block, by least squares on the
    // residuals: u_j = -sum_{i<j} psi_ji u_i + e_j, Psi stored row by row.
    arma::vec psi0(npsi, arma::fill::zeros);
    if (npsi > 0)
    {
        arma::uword pos = 0;
        for (arma::uword j = 1; j < k; ++j)
        {
            const arma::mat lhs = arma::trans(u.rows(0, j - 1));   // tt x j
            arma::vec coefficients;
            if (arma::solve(coefficients, lhs.t() * lhs, lhs.t() * u.row(j).t(), arma::solve_opts::no_approx))
            {
                psi0.subvec(pos, pos + j - 1) = -coefficients;
            }
            pos += j;
        }
        arma::mat Psi = arma::eye<arma::mat>(k, k);
        pos = 0;
        for (arma::uword j = 1; j < k; ++j)
        {
            for (arma::uword i = 0; i < j; ++i)
            {
                Psi(j, i) = psi0(pos++);
            }
        }
        u = Psi * u;
        for (arma::uword i = 0; i < k; ++i)
        {
            residVar(i) = std::max(arma::var(u.row(i).t()), 1e-8);
        }
    }

    // -- The file.
    HighFive::File h5(path.toStdString(), HighFive::File::Overwrite);
    const ModelFile file(h5);
    const bool gamma = s.errors == Errors::Gamma;
    const std::string varsel = s.selection == Selection::Bvs    ? "bvs"
                               : s.selection == Selection::Ssvs ? "ssvs"
                                                                : "none";

    writeAttribute<std::string>(file, "/model", "algorithm", s.algorithm().toStdString());
    writeAttribute<int>(file, "/model", "k", l.k);
    writeAttribute<int>(file, "/model", "p", l.p);
    writeAttribute<int>(file, "/model", "m", 0);
    writeAttribute<int>(file, "/model", "s", 0);
    writeAttribute<int>(file, "/model", "n", l.n);
    if (l.vec)
    {
        writeAttribute<int>(file, "/model", "rank", l.rank);
        writeAttribute<int>(file, "/model", "k_beta", l.kBeta);
        writeAttribute<int>(file, "/model", "n_restricted", l.nRestricted);
    }
    writeAttribute<int>(file, "/model", "iterations", s.iterations);
    writeAttribute<int>(file, "/model", "burnin", s.burnin);
    writeAttribute<int>(file, "/model", "thin", s.thin);
    writeAttribute<std::string>(file, "/model", "varsel", varsel);
    writeAttribute<bool>(file, "/model", "structural", s.structural);
    const std::string prefix = s.errors == Errors::Wishart ? "wishart" : gamma ? "gamma" : "sv";
    writeAttribute<std::string>(file, "/model", "error", s.covar ? prefix + "+covar" : prefix);
    if (s.seed)
    {
        // An integer, as bayests-r stores one, so R reads it back as one.
        writeAttribute<int>(file, "/model", "seed", static_cast<int>(*s.seed));
    }

    // Data: the response as a time series, the SUR regressors with a name per
    // column, and a VEC's error correction term with a name per row of beta.
    const TimeIndex sample = timeIndex(s.data, l.estFirst, l.estLast);
    writeSeries(file, "/data/train/y", response, vars, sample);
    writeMat(file, "/data/train/z", z);
    writeDatasetAttribute(file, "/data/train/z", "coefficients", names);
    if (l.vec)
    {
        writeMat(file, "/data/train/w", w);   // (tt, k_beta): (k_beta, tt) on disk
        writeDatasetAttribute(file, "/data/train/w", "terms", terms);
    }

    if (s.horizon > 0)
    {
        // (h, n_x) on paper. A VEC's is the *level* layout: p blocks of level
        // lags, the unrestricted terms, then the restricted ones (vec_to_var.h).
        // Lag j at horizon i comes from the data while j > i; the sampler
        // overwrites the rest with the path it simulates.
        const int levelLags = l.p;
        const int width = l.k * levelLags + l.n + l.nRestricted;
        arma::mat fx(static_cast<arma::uword>(s.horizon), static_cast<arma::uword>(width), arma::fill::zeros);
        for (int i = 0; i < s.horizon; ++i)
        {
            const auto hi = static_cast<arma::uword>(i);
            for (int lag = 1; lag <= levelLags; ++lag)
            {
                if (lag > i)
                {
                    const auto first = static_cast<arma::uword>((lag - 1) * l.k);
                    fx.submat(hi, first, hi, first + k - 1) =
                        y.row(static_cast<arma::uword>(l.estLast + 1 + i - lag));
                }
            }
            arma::uword d = static_cast<arma::uword>(l.k * levelLags);
            const double trendValue = static_cast<double>(l.tt + i + 1);
            if (l.unrestrictedConstant)
            {
                fx(hi, d++) = 1.0;
            }
            if (l.unrestrictedTrend)
            {
                fx(hi, d++) = trendValue;
            }
            if (l.restrictedConstant)
            {
                fx(hi, d++) = 1.0;
            }
            if (l.restrictedTrend)
            {
                fx(hi, d++) = trendValue;
            }
        }
        writeMat(file, "/data/forecast/x", fx);
        writeAttribute<int>(file, "/model", "h", s.horizon);
        if (l.testRows > 0)
        {
            // What the held-out periods realised -- in levels for a VEC too,
            // which is scored in its level parameterisation.
            const arma::mat realised = y.rows(static_cast<arma::uword>(l.estLast + 1),
                                              static_cast<arma::uword>(l.estLast + l.testRows));
            writeSeries(file, "/data/test/y", realised, vars,
                        timeIndex(s.data, l.estLast + 1, l.estLast + l.testRows));
        }
    }

    // Coefficient prior and starting values. A constant VEC's loadings must be
    // zero-centred and uncoupled (their block is rebuilt from /priors/beta every
    // draw), which a zero mean and a diagonal precision are.
    writeRow(file, "/priors/a/mu", arma::vec(nparams, arma::fill::zeros));
    writeMat(file, "/priors/a/v_inv", arma::diagmat(precision));
    if (s.timeVarying)
    {
        arma::vec stateVariance(nparams);
        if (s.nonCentred)
        {
            writeRow(file, "/priors/a/omega_v", arma::vec(nparams, arma::fill::value(s.omegaV)));
            stateVariance.fill(s.omegaV);
        }
        else
        {
            arma::vec rate(nparams, arma::fill::value(s.stateRate));
            rate.elem(arma::find(isDet)).fill(s.stateRateDet);
            writeRow(file, "/priors/a/shape", arma::vec(nparams, arma::fill::value(s.stateShape)));
            writeRow(file, "/priors/a/rate", rate);
            stateVariance = rate / s.stateShape;   // the chain starts at the prior mean precision
        }
        writeMat(file, "/initial/a", arma::repmat(ols, 1, tt));   // (nparams, tt): the path
        writeMat(file, "/initial/a_sigma_inv", arma::diagmat(1.0 / stateVariance));
        writeRow(file, "/initial/a_init", ols);
    }
    else
    {
        writeRow(file, "/initial/a", ols);
    }

    // The cointegration space.
    if (l.vec)
    {
        const arma::vec betaVec = arma::vectorise(beta);   // vec(beta), column by column
        if (s.timeVarying)
        {
            // The noninformative prior of bvartools' cointspace_prior(): the
            // state before the sample at the stationary distribution of
            // beta_t = rho beta_{t-1} + eta_t, eta_t ~ N(0, I).
            const double stationary = 1.0 - s.cointRho * s.cointRho;
            writeRow(file, "/priors/beta/mu", arma::vec(nBeta, arma::fill::zeros));
            writeMat(file, "/priors/beta/v_inv", stationary * arma::eye<arma::mat>(nBeta, nBeta));
            writeScalar<double>(file, "/priors/beta/rho", s.cointRho);
            writeMat(file, "/initial/beta", arma::repmat(betaVec, 1, tt));   // (tt, n_beta) on disk
            writeRow(file, "/initial/beta_init", betaVec);
        }
        else
        {
            // Koop, León-González and Strachan (2010).
            writeScalar<double>(file, "/priors/beta/v_inv", s.cointShrinkage);
            writeMat(file, "/priors/beta/p_tau_inv",
                     s.cointPTauInv * arma::eye<arma::mat>(static_cast<arma::uword>(l.kBeta),
                                                           static_cast<arma::uword>(l.kBeta)));
            writeRow(file, "/initial/beta", betaVec);
        }
    }

    // Variable selection -- never on a VEC's loadings, whose exclusion would be
    // a change of rank.
    if (s.selection != Selection::None)
    {
        std::vector<int> include;
        for (arma::uword q = nAlpha; q < nparams; ++q)
        {
            if (s.selectDeterministic || isDet(q) == 0)
            {
                include.push_back(static_cast<int>(q) + 1);   // one-based
            }
        }
        writeRow(file, "/priors/a/inprior", arma::vec(nparams, arma::fill::value(s.inclusionPrior)));
        writeIntRow(file, "/priors/a/include", include);
        writeRow(file, "/initial/a_lambda", arma::vec(nparams, arma::fill::ones));
        if (s.selection == Selection::Ssvs)
        {
            // The semiautomatic approach of George, Sun and Ni (2008, Sec. 2.2).
            writeRow(file, "/priors/a/tau0", s.ssvsTau0 * olsSe);
            writeRow(file, "/priors/a/tau1", s.ssvsTau1 * olsSe);
        }
    }

    // Covariance block
    if (npsi > 0)
    {
        writeRow(file, "/priors/psi/mu", arma::vec(npsi, arma::fill::zeros));
        writeMat(file, "/priors/psi/v_inv", s.covarPrecision * arma::eye<arma::mat>(npsi, npsi));
        if (s.timeVarying)
        {
            arma::vec stateVariance(npsi);
            if (s.nonCentred)
            {
                writeRow(file, "/priors/psi/omega_v", arma::vec(npsi, arma::fill::value(s.omegaV)));
                stateVariance.fill(s.omegaV);
            }
            else
            {
                writeRow(file, "/priors/psi/shape", arma::vec(npsi, arma::fill::value(s.stateShape)));
                writeRow(file, "/priors/psi/rate", arma::vec(npsi, arma::fill::value(s.stateRate)));
                stateVariance.fill(s.stateRate / s.stateShape);
            }
            writeMat(file, "/initial/psi", arma::repmat(psi0, 1, tt));
            writeMat(file, "/initial/psi_sigma_inv", arma::diagmat(1.0 / stateVariance));
            writeRow(file, "/initial/psi_init", psi0);
        }
        else
        {
            writeRow(file, "/initial/psi", psi0);
        }

        // The block's own selection scheme, so it can differ from the
        // coefficients' (model-file.md, /model/priors/psi).
        const bool selectPsi = s.selection != Selection::None && s.selectCovariances;
        writeAttribute<std::string>(file, "/model/priors/psi", "varsel", selectPsi ? varsel : std::string("none"));
        if (selectPsi)
        {
            std::vector<int> include(npsi);
            for (arma::uword q = 0; q < npsi; ++q)
            {
                include[q] = static_cast<int>(q) + 1;
            }
            writeRow(file, "/priors/psi/inprior", arma::vec(npsi, arma::fill::value(s.inclusionPrior)));
            writeIntRow(file, "/priors/psi/include", include);
            writeRow(file, "/initial/psi_lambda", arma::vec(npsi, arma::fill::ones));
            if (s.selection == Selection::Ssvs)
            {
                writeRow(file, "/priors/psi/tau0", arma::vec(npsi, arma::fill::value(s.ssvsTau0)));
                writeRow(file, "/priors/psi/tau1", arma::vec(npsi, arma::fill::value(s.ssvsTau1)));
            }
        }
    }

    // Errors
    switch (s.errors)
    {
    case Errors::Wishart:
    {
        writeScalar<int>(file, "/priors/u_sigma/df", l.k + s.wishartDfExtra);
        writeMat(file, "/priors/u_sigma/scale", s.wishartScale * arma::eye<arma::mat>(k, k));
        arma::mat sigmaInv;
        if (!arma::inv_sympd(sigmaInv, arma::mat(u * u.t() / static_cast<double>(tt))))
        {
            sigmaInv = arma::diagmat(1.0 / residVar);
        }
        writeMat(file, "/initial/u_sigma_inv", sigmaInv);
        break;
    }
    case Errors::Gamma:
        writeRow(file, "/priors/u_sigma/shape", arma::vec(k, arma::fill::value(s.gammaShape)));
        writeRow(file, "/priors/u_sigma/rate", arma::vec(k, arma::fill::value(s.gammaRate)));
        // The constant gamma models start from u_sigma_inv, the time-varying
        // ones from u_omega_inv (model-file.md, /initial); both diagonal.
        writeMat(file, s.timeVarying ? "/initial/u_omega_inv" : "/initial/u_sigma_inv",
                 arma::diagmat(1.0 / residVar));
        break;
    case Errors::Stochvol:
    {
        writeRow(file, "/priors/u_sigma/offset", arma::vec(k, arma::fill::value(s.svOffset)));
        writeRow(file, "/priors/u_sigma/sigma", arma::vec(k, arma::fill::value(s.svStateVariance)));
        if (s.timeVarying && s.nonCentred)
        {
            writeRow(file, "/priors/u_sigma/omega_v", arma::vec(k, arma::fill::value(s.svOmegaV)));
        }
        else
        {
            writeRow(file, "/priors/u_sigma/shape", arma::vec(k, arma::fill::value(s.svShape)));
            writeRow(file, "/priors/u_sigma/rate", arma::vec(k, arma::fill::value(s.svRate)));
        }
        writeRow(file, "/priors/u_sigma/mu", arma::vec(k, arma::fill::value(s.svMu)));
        writeMat(file, "/priors/u_sigma/v_inv", s.svPrecision * arma::eye<arma::mat>(k, k));
        const arma::vec logVar = arma::log(residVar);
        writeMat(file, "/initial/h", arma::repmat(logVar.t(), tt, 1));   // (tt, k): (k, tt) on disk
        writeRow(file, "/initial/h_init", logVar);
        break;
    }
    }
}

} // namespace studio
