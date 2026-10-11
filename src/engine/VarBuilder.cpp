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
    std::string partial;
    std::size_t start = 1;
    while (start <= path.size())
    {
        const std::size_t slash = path.find('/', start);
        partial = path.substr(0, slash == std::string::npos ? path.size() : slash);
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

void writeIntScalar(const ModelFile &file, const std::string &path, int value)
{
    ensureGroup(file, path.substr(0, path.rfind('/')));
    file.createDataSet<int>(path, HighFive::DataSpace::From(value)).write(value);
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

// -- The model's arithmetic.

struct Layout
{
    int k = 0;
    int p = 0;
    int n = 0;          // deterministic terms
    int nx = 0;         // k*p + n
    int nNonStruct = 0; // k*nx
    int nStruct = 0;
    int nparams = 0;
    int npsi = 0;
    int estFirst = 0;   // first estimation row of the data set
    int estLast = 0;    // last estimation row
    int tt = 0;
    int testRows = 0;
};

Layout layoutOf(const VarBuilderSpec &s)
{
    Layout l;
    l.k = static_cast<int>(s.columns.size());
    l.p = s.p;
    l.n = (s.constant ? 1 : 0) + (s.trend ? 1 : 0);
    l.nx = l.k * l.p + l.n;
    l.nNonStruct = l.k * l.nx;
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

/// The endogenous data, data-set rows by model variables.
arma::mat endogenous(const VarBuilderSpec &s)
{
    arma::mat y(s.data.values.n_rows, s.columns.size());
    for (std::size_t j = 0; j < s.columns.size(); ++j)
    {
        y.col(j) = s.data.values.col(static_cast<arma::uword>(s.columns[j]));
    }
    return y;
}

/// x_t of one period: lags 1..p of every variable, then the deterministic terms.
/// `trendValue` counts from 1 at the first estimation period.
arma::vec regressors(const arma::mat &y, const Layout &l, int row, double trendValue, bool constant, bool trend)
{
    arma::vec x(static_cast<arma::uword>(l.nx));
    for (int lag = 1; lag <= l.p; ++lag)
    {
        x.subvec(static_cast<arma::uword>((lag - 1) * l.k), static_cast<arma::uword>(lag * l.k - 1)) =
            arma::trans(y.row(static_cast<arma::uword>(row - lag)));
    }
    int d = l.k * l.p;
    if (constant)
    {
        x(static_cast<arma::uword>(d++)) = 1.0;
    }
    if (trend)
    {
        x(static_cast<arma::uword>(d++)) = trendValue;
    }
    return x;
}

} // namespace

QString VarBuilderSpec::algorithm() const
{
    QString name = QStringLiteral("Var");
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
    else if (l.tt < 1)
    {
        out.refusal = QObject::tr("The sample is too short for %1 lags.").arg(s.p);
    }
    else if (s.iterations < 1 || s.burnin < 0 || s.thin < 1)
    {
        out.refusal = QObject::tr("Draws must be positive, the burn-in non-negative, the thinning at least 1.");
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
    const arma::mat y = endogenous(s);

    // -- y and z, stacked by period: per period a kron(x_t', I_k) block and, for
    // a structural model, -y_t of every variable in the equations after it --
    // the columns bvartools' create_bvarmodel() builds.
    arma::vec yStacked(tt * k);
    arma::mat z(tt * k, nparams, arma::fill::zeros);
    const arma::mat eye = arma::eye<arma::mat>(k, k);
    for (arma::uword t = 0; t < tt; ++t)
    {
        const int row = l.estFirst + static_cast<int>(t);
        yStacked.subvec(t * k, (t + 1) * k - 1) = arma::trans(y.row(static_cast<arma::uword>(row)));
        const arma::vec x = regressors(y, l, row, static_cast<double>(t + 1), s.constant, s.trend);
        z.submat(t * k, 0, (t + 1) * k - 1, static_cast<arma::uword>(l.nNonStruct) - 1) = arma::kron(x.t(), eye);
        arma::uword col = static_cast<arma::uword>(l.nNonStruct);
        for (arma::uword j = 0; j + 1 < k && s.structural; ++j)
        {
            for (arma::uword i = j + 1; i < k; ++i)
            {
                z(t * k + i, col++) = -y(static_cast<arma::uword>(row), j);
            }
        }
    }

    // -- A name per coefficient, for the views: "income <- invest(-1)".
    std::vector<std::string> names;
    {
        std::vector<std::string> vars;
        for (const int c : s.columns)
        {
            vars.push_back(s.data.names[c].toStdString());
        }
        std::vector<std::string> regressor;
        for (int lag = 1; lag <= l.p; ++lag)
        {
            for (const auto &v : vars)
            {
                regressor.push_back(v + "(-" + std::to_string(lag) + ")");
            }
        }
        if (s.constant)
        {
            regressor.push_back("constant");
        }
        if (s.trend)
        {
            regressor.push_back("trend");
        }
        for (const auto &r : regressor)
        {
            for (const auto &eq : vars)
            {
                names.push_back(eq + " \u2190 " + r);
            }
        }
        for (arma::uword j = 0; j + 1 < k && s.structural; ++j)
        {
            for (arma::uword i = j + 1; i < k; ++i)
            {
                names.push_back(vars[i] + " \u2190 " + vars[j] + " (contemporaneous)");
            }
        }
    }

    // -- Least squares, for the starting values and the Minnesota scales.
    arma::vec ols(nparams, arma::fill::zeros);
    arma::vec olsSe(nparams, arma::fill::ones);
    bool haveOls = false;
    if (tt * k > nparams)
    {
        arma::mat ztz = z.t() * z;
        arma::mat ztzInv;
        if (arma::inv_sympd(ztzInv, ztz))
        {
            ols = ztzInv * (z.t() * yStacked);
            haveOls = true;
        }
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
        arma::mat ztzInv = arma::inv_sympd(z.t() * z);
        for (arma::uword q = 0; q < nparams; ++q)
        {
            olsSe(q) = std::sqrt(std::max(ztzInv(q, q) * residVar(q % k), 1e-12));
        }
    }

    // -- The prior precision of every coefficient. Coefficient q*k + i is
    // regressor q in equation i, which is what kron(x', I_k) lays out.
    arma::vec precision(nparams);
    arma::uvec isDet(nparams, arma::fill::zeros);
    for (arma::uword q = 0; q < static_cast<arma::uword>(l.nx); ++q)
    {
        const bool det = q >= static_cast<arma::uword>(l.k * l.p);
        for (arma::uword i = 0; i < k; ++i)
        {
            const arma::uword pos = q * k + i;
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
    for (arma::uword q = static_cast<arma::uword>(l.nNonStruct); q < nparams; ++q)
    {
        precision(q) = s.coefPrecision;
    }

    // -- Starting values of the covariance block, by least squares on the
    // residuals: u_j = -sum_{i<j} psi_ji u_i + e_j, Psi stored row by row.
    arma::vec psi0(npsi, arma::fill::zeros);
    if (npsi > 0)
    {
        arma::uword pos = 0;
        for (arma::uword j = 1; j < k; ++j)
        {
            const arma::mat x = arma::trans(u.rows(0, j - 1));  // tt x j
            arma::vec beta;
            if (arma::solve(beta, x.t() * x, x.t() * u.row(j).t(), arma::solve_opts::no_approx))
            {
                psi0.subvec(pos, pos + j - 1) = -beta;
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
    const std::string algorithm = s.algorithm().toStdString();
    const bool gamma = s.errors == Errors::Gamma;
    const std::string varsel = s.selection == Selection::Bvs    ? "bvs"
                               : s.selection == Selection::Ssvs ? "ssvs"
                                                                : "none";

    writeAttribute<std::string>(file, "/model", "algorithm", algorithm);
    writeAttribute<int>(file, "/model", "k", l.k);
    writeAttribute<int>(file, "/model", "p", l.p);
    writeAttribute<int>(file, "/model", "m", 0);
    writeAttribute<int>(file, "/model", "s", 0);
    writeAttribute<int>(file, "/model", "n", l.n);
    writeAttribute<int>(file, "/model", "iterations", s.iterations);
    writeAttribute<int>(file, "/model", "burnin", s.burnin);
    writeAttribute<int>(file, "/model", "thin", s.thin);
    writeAttribute<std::string>(file, "/model", "varsel", varsel);
    writeAttribute<bool>(file, "/model", "structural", s.structural);
    const std::string prefix = s.errors == Errors::Wishart ? "wishart" : gamma ? "gamma" : "sv";
    writeAttribute<std::string>(file, "/model", "error", s.covar ? prefix + "+covar" : prefix);
    if (s.seed)
    {
        // An unsigned 64-bit attribute; read_model_seed() takes any whole number.
        writeAttribute<std::uint64_t>(file, "/model", "seed", *s.seed);
    }

    // Data
    writeRow(file, "/data/train/y", yStacked);
    writeMat(file, "/data/train/z", z);
    {
        // One name per column of z, i.e. per coefficient; no reader looks at
        // attributes of a dataset, so this is for people and the studio.
        HighFive::DataSet zs = file.getDataSet("/data/train/z");
        zs.createAttribute<std::string>("coefficients", HighFive::DataSpace::From(names)).write(names);
    }
    {
        // Names travel as an attribute on the data, where no reader looks.
        std::vector<std::string> names;
        for (const int c : s.columns)
        {
            names.push_back(s.data.names[c].toStdString());
        }
        HighFive::DataSet ds = file.getDataSet("/data/train/y");
        ds.createAttribute<std::string>("variables", HighFive::DataSpace::From(names)).write(names);
        const std::string first = s.data.periodLabel(l.estFirst).toStdString();
        const std::string last = s.data.periodLabel(l.estLast).toStdString();
        ds.createAttribute<std::string>("first_period", HighFive::DataSpace::From(first)).write(first);
        ds.createAttribute<std::string>("last_period", HighFive::DataSpace::From(last)).write(last);
    }

    if (s.horizon > 0)
    {
        // (h, n_x) on paper. Lag l at horizon i comes from the data while
        // l > i; the sampler overwrites the rest with the path it simulates.
        arma::mat x(static_cast<arma::uword>(s.horizon), static_cast<arma::uword>(l.nx), arma::fill::zeros);
        for (int i = 0; i < s.horizon; ++i)
        {
            for (int lag = 1; lag <= l.p; ++lag)
            {
                if (lag > i)
                {
                    const int row = l.estLast + 1 + i - lag;
                    x.submat(static_cast<arma::uword>(i), static_cast<arma::uword>((lag - 1) * l.k),
                             static_cast<arma::uword>(i), static_cast<arma::uword>(lag * l.k - 1)) =
                        y.row(static_cast<arma::uword>(row));
                }
            }
            int d = l.k * l.p;
            if (s.constant)
            {
                x(static_cast<arma::uword>(i), static_cast<arma::uword>(d++)) = 1.0;
            }
            if (s.trend)
            {
                x(static_cast<arma::uword>(i), static_cast<arma::uword>(d++)) = static_cast<double>(l.tt + i + 1);
            }
        }
        writeMat(file, "/data/forecast/x", x);
        writeAttribute<int>(file, "/model", "h", s.horizon);
        if (l.testRows > 0)
        {
            writeMat(file, "/data/test/y",
                     y.rows(static_cast<arma::uword>(l.estLast + 1),
                            static_cast<arma::uword>(l.estLast + l.testRows)));
        }
    }

    // Coefficient prior and starting values
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
        writeMat(file, "/initial/a", arma::repmat(ols, 1, tt));            // (nparams, tt): the path
        writeMat(file, "/initial/a_sigma_inv", arma::diagmat(1.0 / stateVariance));
        writeRow(file, "/initial/a_init", ols);
    }
    else
    {
        writeRow(file, "/initial/a", ols);
    }

    // Variable selection
    if (s.selection != Selection::None)
    {
        std::vector<int> include;
        for (arma::uword q = 0; q < nparams; ++q)
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
        writeIntScalar(file, "/priors/u_sigma/df", l.k + s.wishartDfExtra);
        writeMat(file, "/priors/u_sigma/scale", s.wishartScale * arma::eye<arma::mat>(k, k));
        arma::mat sigma = u * u.t() / static_cast<double>(tt);
        arma::mat sigmaInv;
        if (!arma::inv_sympd(sigmaInv, sigma))
        {
            sigmaInv = arma::diagmat(1.0 / residVar);
        }
        writeMat(file, "/initial/u_sigma_inv", sigmaInv);
        break;
    }
    case Errors::Gamma:
        writeRow(file, "/priors/u_sigma/shape", arma::vec(k, arma::fill::value(s.gammaShape)));
        writeRow(file, "/priors/u_sigma/rate", arma::vec(k, arma::fill::value(s.gammaRate)));
        // The constant gamma model starts from u_sigma_inv, the time-varying
        // one from u_omega_inv (model-file.md, /initial); both diagonal.
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
