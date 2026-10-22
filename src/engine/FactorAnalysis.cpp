// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Franz X. Mohr

#include "engine/FactorAnalysis.h"

#include "engine/ModelFileTree.h"

#include <QObject>

#include <highfive/highfive.hpp>

#include <algorithm>
#include <cmath>
#include <mutex>
#include <stdexcept>

namespace studio
{

namespace
{

/// The share and the responses read every draw's matrices; more than this many
/// draws, thinned evenly, move a band by less than its own Monte Carlo error.
constexpr int kMaxDraws = 4000;

std::runtime_error failure(const QString &message)
{
    return std::runtime_error(message.toStdString());
}

bool hasDataset(const QString &file, const QString &path)
{
    const HighFive::File h5(file.toStdString(), HighFive::File::ReadOnly);
    return h5.exist(path.toStdString());
}

std::vector<int> drawSubset(int draws)
{
    std::vector<int> pick;
    const int n = std::min(draws, kMaxDraws);
    for (int i = 0; i < n; ++i)
    {
        pick.push_back(static_cast<int>((static_cast<long long>(i) * draws) / n));
    }
    return pick;
}

double quantileOfSorted(const std::vector<double> &sorted, double q)
{
    if (sorted.empty())
    {
        return std::nan("");
    }
    const double pos = q * static_cast<double>(sorted.size() - 1);
    const std::size_t lo = static_cast<std::size_t>(std::floor(pos));
    const std::size_t hi = std::min(lo + 1, sorted.size() - 1);
    return sorted[lo] + (pos - static_cast<double>(lo)) * (sorted[hi] - sorted[lo]);
}

/// Everything about the file the functions below need, read once.
struct Model
{
    FactorSupport support;
    int ns = 0;                 ///< the state's width
    arma::mat panel;            ///< k x tt, the data
    arma::mat deterministic;    ///< n x tt, or empty
    arma::mat observed;         ///< n_obs x tt, a FAVAR's observed factors
};

Model readModel(const QString &file)
{
    Model out;
    FactorSupport &s = out.support;
    {
        const HighFive::File h5(file.toStdString(), HighFive::File::ReadOnly);
        const HighFive::Group model = h5.getGroup("/model");
        if (!model.hasAttribute("algorithm"))
        {
            throw failure(QObject::tr("The file names no algorithm."));
        }
        s.algorithm = QString::fromStdString(model.getAttribute("algorithm").read<std::string>());
    }
    s.favar = s.algorithm.startsWith(QLatin1String("Favar"));
    if (!s.favar && !s.algorithm.startsWith(QLatin1String("Dfm")))
    {
        throw failure(QObject::tr("%1 is not a factor model.").arg(s.algorithm));
    }
    if (!hasDataset(file, QStringLiteral("/posterior/factors/coeffs")) ||
        !hasDataset(file, QStringLiteral("/posterior/lambda/coeffs")))
    {
        throw failure(QObject::tr("Estimate the model first: it has no posterior draws of the factors."));
    }
    s.k = readGroupInt(file, QStringLiteral("/model"), QStringLiteral("k"), 0);
    s.factors = readGroupInt(file, QStringLiteral("/model"), QStringLiteral("n_factors"), 0);
    s.observedFactors = s.favar ? readGroupInt(file, QStringLiteral("/model"), QStringLiteral("n_obs_factors"), 0) : 0;
    s.p = readGroupInt(file, QStringLiteral("/model"), QStringLiteral("p"), 0);
    if (s.k < 1 || s.factors < 1)
    {
        throw failure(QObject::tr("The file gives no panel or no factors."));
    }
    out.ns = s.factors + s.observedFactors;
    out.panel = panelByPeriod(file, s.k);
    s.periods = static_cast<int>(out.panel.n_cols);
    s.timeVarying = s.algorithm.startsWith(QLatin1String("DfmTvp"));
    s.stochvol = s.algorithm.endsWith(QLatin1String("Stochvol"));

    const int n = readGroupInt(file, QStringLiteral("/model"), QStringLiteral("n"), 0);
    if (n > 0 && hasDataset(file, QStringLiteral("/data/train/x")) && hasDataset(file, QStringLiteral("/posterior/c/coeffs")))
    {
        out.deterministic = readDraws(file, QStringLiteral("/data/train/x"));
    }
    if (s.favar)
    {
        out.observed = readDraws(file, QStringLiteral("/data/train/f_obs"));
        s.observed = readStringsAttribute(file, QStringLiteral("/data/train/f_obs"), QStringLiteral("variables"));
        for (int i = static_cast<int>(s.observed.size()); i < s.observedFactors; ++i)
        {
            s.observed << QObject::tr("Observed %1").arg(i + 1);
        }
    }

    s.series = readStringsAttribute(file, QStringLiteral("/data/train/y"), QStringLiteral("variables"));
    for (int i = static_cast<int>(s.series.size()); i < s.k; ++i)
    {
        s.series << QObject::tr("Series %1").arg(i + 1);
    }
    for (int i = 0; i < s.factors; ++i)
    {
        s.state << QObject::tr("Factor %1").arg(i + 1);
    }
    s.state << s.observed;

    s.time = samplePeriodTimes(file, s.periods);

    s.responses = s.p > 0 && hasDataset(file, QStringLiteral("/posterior/a/coeffs")) &&
                  hasDataset(file, QStringLiteral("/posterior/v_sigma_inv/coeffs"));
    if (!s.responses)
    {
        s.responsesReason = s.p == 0 ? QObject::tr("The state has no transition (p = 0): a shock dies on impact.")
                                     : QObject::tr("The file has no draws of the state equation.");
    }
    s.ok = true;
    return out;
}

/// Period `t`'s block of a stacked dataset, or the whole column where the
/// dataset holds one block: rows t*width .. (t+1)*width-1 of column `d`.
arma::vec blockOf(const arma::mat &draws, int width, int t, int d)
{
    const arma::uword w = static_cast<arma::uword>(width);
    if (draws.n_rows == w)
    {
        return draws.col(static_cast<arma::uword>(d));
    }
    const arma::uword first = static_cast<arma::uword>(t) * w;
    return draws.submat(first, static_cast<arma::uword>(d), first + w - 1, static_cast<arma::uword>(d));
}

int lastPeriod(const Model &m, int period)
{
    const int last = m.support.periods - 1;
    return period < 0 || period > last ? last : period;
}

} // namespace

QString modelAlgorithm(const QString &file)
{
    const std::lock_guard<std::recursive_mutex> hdf5(hdf5Mutex());
    try
    {
        const HighFive::File h5(file.toStdString(), HighFive::File::ReadOnly);
        if (h5.exist("/model"))
        {
            const HighFive::Group model = h5.getGroup("/model");
            if (model.hasAttribute("algorithm"))
            {
                return QString::fromStdString(model.getAttribute("algorithm").read<std::string>());
            }
        }
    }
    catch (const std::exception &)
    {
    }
    return {};
}

arma::mat readDraws(const QString &file, const QString &path)
{
    const Matrix m = readMatrix(file, path);
    arma::mat out(m.rows, m.cols);
    for (std::size_t r = 0; r < m.rows; ++r)
    {
        for (std::size_t c = 0; c < m.cols; ++c)
        {
            out(r, c) = m.at(r, c);
        }
    }
    return out;
}

arma::mat panelByPeriod(const QString &file, int k)
{
    const Matrix m = readMatrix(file, QStringLiteral("/data/train/y"));
    if (m.rows == 1 && k > 0 && m.cols % static_cast<std::size_t>(k) == 0)
    {
        const std::size_t tt = m.cols / static_cast<std::size_t>(k);
        arma::mat out(static_cast<arma::uword>(k), tt);
        for (std::size_t t = 0; t < tt; ++t)
        {
            for (int i = 0; i < k; ++i)
            {
                out(static_cast<arma::uword>(i), t) = m.values[t * static_cast<std::size_t>(k) + static_cast<std::size_t>(i)];
            }
        }
        return out;
    }
    if (m.rows == static_cast<std::size_t>(k))
    {
        return readDraws(file, QStringLiteral("/data/train/y"));
    }
    throw failure(QObject::tr("/data/train/y is neither stacked by period nor one row per series."));
}

std::vector<double> samplePeriodTimes(const QString &file, int periods)
{
    double start = 1.0, frequency = 1.0;
    const HighFive::File h5(file.toStdString(), HighFive::File::ReadOnly);
    const HighFive::DataSet y = h5.getDataSet("/data/train/y");
    if (y.hasAttribute("tsp"))
    {
        const auto tsp = y.getAttribute("tsp").read<std::vector<double>>();
        if (tsp.size() == 3 && tsp[2] > 0)
        {
            start = tsp[0];
            frequency = tsp[2];
        }
    }
    std::vector<double> out;
    for (int t = 0; t < periods; ++t)
    {
        out.push_back(start + static_cast<double>(t) / frequency);
    }
    return out;
}

PathBands summarisePath(const arma::mat &draws, int width, int row, int periods)
{
    PathBands out;
    std::vector<double> column(draws.n_cols);
    for (int t = 0; t < periods; ++t)
    {
        const arma::uword r = static_cast<arma::uword>(t) * static_cast<arma::uword>(width) + static_cast<arma::uword>(row);
        for (arma::uword d = 0; d < draws.n_cols; ++d)
        {
            column[d] = draws(r, d);
        }
        std::sort(column.begin(), column.end());
        out.lo90.push_back(quantileOfSorted(column, 0.05));
        out.lo68.push_back(quantileOfSorted(column, 0.16));
        out.median.push_back(quantileOfSorted(column, 0.5));
        out.hi68.push_back(quantileOfSorted(column, 0.84));
        out.hi90.push_back(quantileOfSorted(column, 0.95));
    }
    return out;
}

FactorSupport factorSupport(const QString &file)
{
    const std::lock_guard<std::recursive_mutex> hdf5(hdf5Mutex());
    try
    {
        Model m = readModel(file);
        const Matrix f = readMatrix(file, QStringLiteral("/posterior/factors/coeffs"));
        m.support.draws = static_cast<int>(f.cols);
        return m.support;
    }
    catch (const std::exception &e)
    {
        FactorSupport out;
        out.reason = QString::fromUtf8(e.what());
        return out;
    }
}

std::vector<PathBands> factorPaths(const QString &file)
{
    const std::lock_guard<std::recursive_mutex> hdf5(hdf5Mutex());
    const Model m = readModel(file);
    const arma::mat f = readDraws(file, QStringLiteral("/posterior/factors/coeffs"));
    std::vector<PathBands> out;
    for (int j = 0; j < m.support.factors; ++j)
    {
        out.push_back(summarisePath(f, m.support.factors, j, m.support.periods));
    }
    return out;
}

LoadingTable loadingTable(const QString &file, int period)
{
    const std::lock_guard<std::recursive_mutex> hdf5(hdf5Mutex());
    const Model m = readModel(file);
    const int k = m.support.k, ns = m.ns;
    const arma::mat lambda = readDraws(file, QStringLiteral("/posterior/lambda/coeffs"));
    const int t = lastPeriod(m, period);
    LoadingTable out;
    out.median.set_size(k, ns);
    out.lower.set_size(k, ns);
    out.upper.set_size(k, ns);
    std::vector<double> column(lambda.n_cols);
    for (int j = 0; j < ns; ++j)
    {
        for (int i = 0; i < k; ++i)
        {
            for (arma::uword d = 0; d < lambda.n_cols; ++d)
            {
                column[d] = blockOf(lambda, k * ns, t, static_cast<int>(d))(static_cast<arma::uword>(j * k + i));
            }
            std::sort(column.begin(), column.end());
            out.median(i, j) = quantileOfSorted(column, 0.5);
            out.lower(i, j) = quantileOfSorted(column, 0.05);
            out.upper(i, j) = quantileOfSorted(column, 0.95);
        }
    }
    return out;
}

ExplainedShare explainedShare(const QString &file)
{
    const std::lock_guard<std::recursive_mutex> hdf5(hdf5Mutex());
    const Model m = readModel(file);
    const int k = m.support.k, ns = m.ns, n = m.support.factors, tt = m.support.periods;
    const arma::mat lambda = readDraws(file, QStringLiteral("/posterior/lambda/coeffs"));
    const arma::mat factors = readDraws(file, QStringLiteral("/posterior/factors/coeffs"));
    const bool det = !m.deterministic.is_empty();
    const arma::mat c = det ? readDraws(file, QStringLiteral("/posterior/c/coeffs")) : arma::mat();
    const bool moving = lambda.n_rows != static_cast<arma::uword>(k * ns);

    std::vector<std::vector<double>> shares(static_cast<std::size_t>(k));
    for (const int d : drawSubset(static_cast<int>(factors.n_cols)))
    {
        // The state, the unobserved factors and then the observed ones.
        arma::mat state(static_cast<arma::uword>(ns), static_cast<arma::uword>(tt));
        state.rows(0, static_cast<arma::uword>(n) - 1) =
            arma::reshape(factors.col(static_cast<arma::uword>(d)), static_cast<arma::uword>(n), static_cast<arma::uword>(tt));
        if (ns > n)
        {
            state.rows(static_cast<arma::uword>(n), static_cast<arma::uword>(ns) - 1) = m.observed;
        }
        arma::mat common(static_cast<arma::uword>(k), static_cast<arma::uword>(tt));
        if (!moving)
        {
            common = arma::reshape(lambda.col(static_cast<arma::uword>(d)), static_cast<arma::uword>(k), static_cast<arma::uword>(ns)) * state;
        }
        else
        {
            for (int t = 0; t < tt; ++t)
            {
                common.col(static_cast<arma::uword>(t)) =
                    arma::reshape(blockOf(lambda, k * ns, t, d), static_cast<arma::uword>(k), static_cast<arma::uword>(ns)) *
                    state.col(static_cast<arma::uword>(t));
            }
        }
        arma::mat target = m.panel;
        if (det)
        {
            target -= arma::reshape(c.col(static_cast<arma::uword>(d)), static_cast<arma::uword>(k), m.deterministic.n_rows) * m.deterministic;
        }
        for (int i = 0; i < k; ++i)
        {
            const arma::rowvec x = target.row(static_cast<arma::uword>(i));
            const double total = arma::var(x);
            const double residual = arma::var(x - common.row(static_cast<arma::uword>(i)));
            shares[static_cast<std::size_t>(i)].push_back(total > 0 ? 1.0 - residual / total : std::nan(""));
        }
    }
    ExplainedShare out;
    for (auto &s : shares)
    {
        std::sort(s.begin(), s.end());
        out.median.push_back(quantileOfSorted(s, 0.5));
        out.lower.push_back(quantileOfSorted(s, 0.05));
        out.upper.push_back(quantileOfSorted(s, 0.95));
    }
    return out;
}

std::vector<arma::mat> factorResponses(const QString &file, int shock, int horizon, int period)
{
    const std::lock_guard<std::recursive_mutex> hdf5(hdf5Mutex());
    const Model m = readModel(file);
    if (!m.support.responses)
    {
        throw failure(m.support.responsesReason);
    }
    const int k = m.support.k, ns = m.ns, n = m.support.factors, p = m.support.p, nObs = m.support.observedFactors;
    if (shock < 0 || shock >= ns)
    {
        throw failure(QObject::tr("There is no shock %1.").arg(shock + 1));
    }
    const int t = lastPeriod(m, period);
    const arma::mat lambda = readDraws(file, QStringLiteral("/posterior/lambda/coeffs"));
    const arma::mat a = readDraws(file, QStringLiteral("/posterior/a/coeffs"));
    const arma::mat v = readDraws(file, QStringLiteral("/posterior/v_sigma_inv/coeffs"));
    const int nA = ns * ns * p;

    std::vector<arma::mat> out;
    for (const int d : drawSubset(static_cast<int>(a.n_cols)))
    {
        const arma::mat lam = arma::reshape(blockOf(lambda, k * ns, t, d), static_cast<arma::uword>(k), static_cast<arma::uword>(ns));
        const arma::mat A = arma::reshape(blockOf(a, nA, t, d), static_cast<arma::uword>(ns), static_cast<arma::uword>(ns * p));

        // The impact of the shock on the state: a DFM's factor innovations are
        // independent, so one standard deviation of factor `shock`; a FAVAR's
        // covariance is full, and its lower Choleski factor orders the observed
        // factors last.
        arma::vec impact(static_cast<arma::uword>(ns), arma::fill::zeros);
        if (m.support.favar)
        {
            const arma::mat precision = arma::reshape(v.col(static_cast<arma::uword>(d)), static_cast<arma::uword>(ns), static_cast<arma::uword>(ns));
            const arma::mat sigma = arma::inv_sympd(arma::symmatu(precision));
            const arma::mat root = arma::chol(sigma, "lower");
            impact = root.col(static_cast<arma::uword>(shock));
        }
        else
        {
            const arma::vec precision = blockOf(v, n, t, d);
            impact(static_cast<arma::uword>(shock)) = 1.0 / std::sqrt(precision(static_cast<arma::uword>(shock)));
        }

        // Phi_h = sum_l A_l Phi_{h-l}, applied to the impact directly.
        std::vector<arma::vec> state(static_cast<std::size_t>(horizon) + 1);
        state[0] = impact;
        for (int h = 1; h <= horizon; ++h)
        {
            arma::vec s(static_cast<arma::uword>(ns), arma::fill::zeros);
            for (int l = 1; l <= std::min(h, p); ++l)
            {
                s += A.cols(static_cast<arma::uword>((l - 1) * ns), static_cast<arma::uword>(l * ns - 1)) *
                     state[static_cast<std::size_t>(h - l)];
            }
            state[static_cast<std::size_t>(h)] = s;
        }
        arma::mat r(static_cast<arma::uword>(horizon) + 1, static_cast<arma::uword>(k + nObs));
        for (int h = 0; h <= horizon; ++h)
        {
            const arma::vec &s = state[static_cast<std::size_t>(h)];
            r.submat(static_cast<arma::uword>(h), 0, static_cast<arma::uword>(h), static_cast<arma::uword>(k) - 1) = arma::trans(lam * s);
            if (nObs > 0)
            {
                r.submat(static_cast<arma::uword>(h), static_cast<arma::uword>(k), static_cast<arma::uword>(h),
                         static_cast<arma::uword>(k + nObs) - 1) = arma::trans(s.tail(static_cast<arma::uword>(nObs)));
            }
        }
        out.push_back(std::move(r));
    }
    return out;
}

} // namespace studio
