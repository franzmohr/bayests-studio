// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Franz X. Mohr

#include "engine/HistoricalDecomposition.h"

#include "engine/ModelFileTree.h"
#include "engine/PosteriorDraws.h"
#include "engine/Structural.h"

#include <QObject>

#include <highfive/highfive.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace studio
{

namespace
{

using detail::Posterior;

std::vector<int> drawSubset(int draws, int maxDraws)
{
    std::vector<int> pick;
    const int n = std::min(draws, std::max(1, maxDraws));
    for (int i = 0; i < n; ++i)
    {
        pick.push_back(static_cast<int>((static_cast<long long>(i) * draws) / n));
    }
    return pick;
}

/// bvartools' refusals in historical_decomposition.bvarmodel(), less the two
/// on moving systems: those are decomposed period by period.
void checkModel(const QString &file, const Posterior &post)
{
    if (post.structural())
    {
        throw std::runtime_error(
            QObject::tr("Historical decompositions are not available for structural models.").toStdString());
    }
    if (!post.vec() && post.lagOrder() < 1)
    {
        throw std::runtime_error(
            QObject::tr("Historical decompositions need a model with at least one lag.").toStdString());
    }
    const std::lock_guard<std::recursive_mutex> hdf5(hdf5Mutex());
    const HighFive::File h5(file.toStdString(), HighFive::File::ReadOnly);
    if (h5.exist("/posterior/y/coeffs") && post.vec())
    {
        // A VEC's levels would have to be rebuilt from each draw's completion too.
        throw std::runtime_error(QObject::tr("A VEC on a panel not observed whole is not decomposed here yet.")
                                     .toStdString());
    }
}

/// The data of the level form: the response y (tt x k, levels for a VEC)
/// and every regressor in the order of Posterior::fullCoefficients() --
/// [y_{t-1} .. y_{t-p}, x_t .. x_{t-s}, d_t, d^r_{t-1}] -- from the file's
/// compact regressors (/data/train/x, or the first equation's row of each
/// period's block of z where the builder wrote z alone) and a VEC's error
/// correction term, as bvartools' vec_to_var() rebuilds them.
struct LevelData
{
    arma::mat y;
    arma::mat x;
};

LevelData levelData(const QString &file, const Posterior &post)
{
    const int k = post.k();
    const int tt = post.periods();
    const int p = post.lagOrder();
    const int m = post.exogenous();
    const int s = post.exogenousLags();
    const int n = readGroupInt(file, QStringLiteral("/model"), QStringLiteral("n"), 0);
    const int rank = readGroupInt(file, QStringLiteral("/model"), QStringLiteral("rank"), 0);
    const bool vec = post.vec();
    const auto ku = static_cast<arma::uword>(k);
    const auto ttu = static_cast<arma::uword>(tt);

    // The response as stored: a time series (k, tt) or stacked (1, tt k).
    const Matrix yFile = readMatrix(file, QStringLiteral("/data/train/y"));
    arma::mat response(ttu, ku);
    for (arma::uword t = 0; t < ttu; ++t)
    {
        for (arma::uword i = 0; i < ku; ++i)
        {
            response(t, i) = yFile.rows == 1 ? yFile.at(0, t * ku + i) : yFile.at(i, t);
        }
    }

    // The compact regressors.
    const int nCompact = vec ? k * std::max(p - 1, 0) + m * s + n : k * p + m * (s + 1) + n;
    arma::mat compact(ttu, static_cast<arma::uword>(nCompact));
    bool haveX = false;
    {
        const std::lock_guard<std::recursive_mutex> hdf5(hdf5Mutex());
        const HighFive::File h5(file.toStdString(), HighFive::File::ReadOnly);
        haveX = h5.exist("/data/train/x");
    }
    const Matrix xFile = haveX ? readMatrix(file, QStringLiteral("/data/train/x")) : Matrix();
    if (haveX && xFile.rows == static_cast<std::size_t>(nCompact) && xFile.cols == static_cast<std::size_t>(tt))
    {
        for (arma::uword t = 0; t < ttu; ++t)
        {
            for (arma::uword q = 0; q < compact.n_cols; ++q)
            {
                compact(t, q) = xFile.at(q, t);
            }
        }
    }
    else
    {
        // z on paper is (tt k, nparams): row t k of column nAlpha + q k is
        // regressor q of period t, kron(x_t', I_k) laid out.
        const Matrix z = readMatrix(file, QStringLiteral("/data/train/z"));
        const std::size_t nAlpha = vec ? static_cast<std::size_t>(k * rank) : 0;
        if (z.cols != static_cast<std::size_t>(tt * k) || z.rows < nAlpha + static_cast<std::size_t>(nCompact * k))
        {
            throw std::runtime_error(QObject::tr("The regressors of this file do not have the layout its model "
                                                 "attributes describe.").toStdString());
        }
        for (arma::uword t = 0; t < ttu; ++t)
        {
            for (arma::uword q = 0; q < compact.n_cols; ++q)
            {
                compact(t, q) = z.at(nAlpha + q * ku, t * ku);
            }
        }
    }
    if (!vec)
    {
        return {response, compact};
    }

    // A VEC: the levels from y_{t-1} in w and the differences.
    const Matrix wFile = readMatrix(file, QStringLiteral("/data/train/w"));
    const int kBeta = static_cast<int>(wFile.rows);
    const int nRestricted = kBeta - k - m;
    const int levelLags = std::max(p, 1);
    const auto mu = static_cast<arma::uword>(m);
    const int width = k * levelLags + m * (m > 0 ? s + 1 : 0) + n + (rank > 0 ? nRestricted : 0);
    LevelData out;
    out.y.set_size(ttu, ku);
    out.x.set_size(ttu, static_cast<arma::uword>(width));
    for (arma::uword t = 0; t < ttu; ++t)
    {
        arma::vec w(static_cast<arma::uword>(kBeta));
        for (int j = 0; j < kBeta; ++j)
        {
            w(static_cast<arma::uword>(j)) = wFile.at(static_cast<std::size_t>(j), t);
        }
        const arma::vec yLag1 = w.subvec(0, ku - 1);
        out.y.row(t) = arma::trans(yLag1 + response.row(t).t());
        arma::uword col = 0;
        arma::vec level = yLag1;   // y_{t-l}
        for (int l = 1; l <= levelLags; ++l)
        {
            if (l > 1)
            {
                // y_{t-l} = y_{t-l+1} - dy_{t-l+1}, dy_{t-j} the j-th lagged difference.
                level -= compact.row(t).cols(static_cast<arma::uword>(l - 2) * ku,
                                             static_cast<arma::uword>(l - 1) * ku - 1).t();
            }
            out.x.row(t).cols(col, col + ku - 1) = level.t();
            col += ku;
        }
        if (m > 0)
        {
            const arma::uword dx0 = ku * static_cast<arma::uword>(std::max(p - 1, 0));
            const arma::vec xLag1 = w.subvec(ku, ku + mu - 1);
            // x_t = x_{t-1} + dx_t, x_{t-j} = x_{t-j+1} - dx_{t-j+1}.
            const auto dx = [&](int j) {
                return arma::vec(compact.row(t).cols(dx0 + static_cast<arma::uword>(j) * mu,
                                                     dx0 + static_cast<arma::uword>(j + 1) * mu - 1).t());
            };
            out.x.row(t).cols(col, col + mu - 1) = arma::trans(xLag1 + dx(0));
            col += mu;
            arma::vec xl = xLag1;
            for (int j = 1; j <= s; ++j)
            {
                if (j > 1)
                {
                    xl -= dx(j - 1);
                }
                out.x.row(t).cols(col, col + mu - 1) = xl.t();
                col += mu;
            }
        }
        for (int d = 0; d < n; ++d)
        {
            out.x(t, col++) = compact(t, compact.n_cols - static_cast<arma::uword>(n) + static_cast<arma::uword>(d));
        }
        for (int d = 0; rank > 0 && d < nRestricted; ++d)
        {
            out.x(t, col++) = w(ku + mu + static_cast<arma::uword>(d));
        }
    }
    return out;
}

double quantile7(const std::vector<double> &v, double q)
{
    const double h = (static_cast<double>(v.size()) - 1.0) * q;
    const auto lo = static_cast<std::size_t>(std::floor(h));
    const std::size_t hi = std::min(lo + 1, v.size() - 1);
    return v[lo] + (h - static_cast<double>(lo)) * (v[hi] - v[lo]);
}

} // namespace

HdSupport hdSupport(const QString &file)
{
    const std::lock_guard<std::recursive_mutex> hdf5(hdf5Mutex());
    HdSupport out;
    try
    {
        const Posterior post(file);
        checkModel(file, post);
        out.ok = true;
        out.k = post.k();
        out.periods = post.periods();
        out.variables = levelNames(file);
        for (int i = static_cast<int>(out.variables.size()); i < out.k; ++i)
        {
            out.variables << QObject::tr("Variable %1").arg(i + 1);
        }
        out.sign = readSignIdentification(file).has_value();
        out.moving = post.moving();
        const HighFive::File h5(file.toStdString(), HighFive::File::ReadOnly);
        const HighFive::DataSet y = h5.getDataSet("/data/train/y");
        if (y.hasAttribute("tsp"))
        {
            const auto tsp = y.getAttribute("tsp").read<std::vector<double>>();
            if (tsp.size() == 3 && tsp[2] > 0)
            {
                out.start = tsp[0];
                out.frequency = tsp[2];
            }
        }
    }
    catch (const std::exception &e)
    {
        out.ok = false;
        out.reason = QString::fromUtf8(e.what());
    }
    return out;
}

namespace
{

/// One draw's decomposition of response `r` through each period's own
/// system: columns 0..k-1 the shocks' contributions, column k the response
/// itself (the caller turns it into the baseline). The completion of a panel
/// not observed whole is that draw's, as in the constant case.
arma::mat movingDecomposition(const detail::Posterior &post, const LevelData &data, const Matrix &completed, int d,
                              arma::uword r, int p)
{
    const auto ku = static_cast<arma::uword>(post.k());
    const arma::uword tt = data.y.n_rows;
    arma::mat y = data.y;
    arma::mat x = data.x;
    if (completed.rows > 0)
    {
        for (arma::uword t = 0; t < tt; ++t)
        {
            for (arma::uword i = 0; i < ku; ++i)
            {
                y(t, i) = completed.at(t * ku + i, static_cast<std::size_t>(d));
            }
        }
        for (arma::uword t = 0; t < tt; ++t)
        {
            for (arma::uword l = 1; l <= static_cast<arma::uword>(p) && l <= t; ++l)
            {
                x.row(t).cols((l - 1) * ku, l * ku - 1) = y.row(t - l);
            }
        }
    }
    arma::mat history(ku * static_cast<arma::uword>(p), ku, arma::fill::zeros);   // M_{t-1} over M_{t-p}
    arma::mat one(tt, ku + 1, arma::fill::zeros);
    for (arma::uword t = 0; t < tt; ++t)
    {
        const int period = static_cast<int>(t) + 1;
        const detail::System s = post.system(d, period);
        const arma::mat P = arma::trans(arma::chol(s.Sigma));
        const arma::mat full = post.fullCoefficients(d, period);
        if (full.n_cols != data.x.n_cols)
        {
            throw std::runtime_error(QObject::tr("The coefficients and the regressors of this file do not "
                                                 "match.").toStdString());
        }
        const arma::rowvec u = y.row(t) - x.row(t) * full.t();
        const arma::rowvec e = u * arma::trans(arma::inv(P));
        const arma::mat aLag = full.cols(0, ku * static_cast<arma::uword>(p) - 1);
        const arma::mat mt = aLag * history + P * arma::diagmat(e);
        one.row(t).cols(0, ku - 1) = mt.row(r);
        if (p > 1)
        {
            history.rows(ku, history.n_rows - 1) = arma::mat(history.rows(0, history.n_rows - ku - 1));
        }
        history.rows(0, ku - 1) = mt;
    }
    one.col(ku) = y.col(r);
    return one;
}

} // namespace

HistoricalDecomposition historicalDecomposition(const QString &file, const HdRequest &request)
{
    const std::lock_guard<std::recursive_mutex> hdf5(hdf5Mutex());
    const Posterior post(file);
    checkModel(file, post);
    if (request.type == HdType::Sign && !readSignIdentification(file))
    {
        throw std::runtime_error(
            QObject::tr("The model carries no sign restricted identification yet.").toStdString());
    }
    if (request.type == HdType::Sign && post.moving())
    {
        throw std::runtime_error(QObject::tr("This model's system moves from period to period, and the sign "
                                             "restrictions' rotations were found against one period's covariance: "
                                             "they identify no other. Choose the Choleski shocks.")
                                     .toStdString());
    }
    const int k = post.k();
    if (request.response < 0 || request.response >= k)
    {
        throw std::runtime_error(QObject::tr("No such response.").toStdString());
    }
    const LevelData data = levelData(file, post);
    const auto ku = static_cast<arma::uword>(k);
    const arma::uword tt = data.y.n_rows;
    const auto r = static_cast<arma::uword>(request.response);
    const int p = post.levelLags();
    // A panel not observed whole: every draw comes with its completion,
    // /posterior/y/coeffs (k tt, draws), stacked by period. Its response and,
    // inside the sample, its lags are that draw's; the presample is what the
    // file conditions on.
    Matrix completed;
    {
        const HighFive::File h5(file.toStdString(), HighFive::File::ReadOnly);
        if (h5.exist("/posterior/y/coeffs"))
        {
            completed = readMatrix(file, QStringLiteral("/posterior/y/coeffs"));
            if (completed.rows != tt * ku || completed.cols != static_cast<std::size_t>(post.draws()))
            {
                throw std::runtime_error(QObject::tr("The completed panel of this file does not have the layout "
                                                     "its model attributes describe.").toStdString());
            }
        }
    }
    arma::vec dataMean(tt, arma::fill::zeros);

    std::vector<arma::mat> draws;
    const bool moving = post.moving();
    for (const int d : drawSubset(post.draws(), request.maxDraws))
    {
        if (moving)
        {
            draws.push_back(movingDecomposition(post, data, completed, d, r, p));
            dataMean += draws.back().col(ku);   // the response, kept there for now
            draws.back().col(ku) -= arma::sum(draws.back().cols(0, ku - 1), 1);
            continue;
        }
        const detail::System s = post.system(d, -1);
        arma::mat P = arma::trans(arma::chol(s.Sigma));
        if (request.type == HdType::Sign)
        {
            const arma::mat Q = post.rotation(file, d);
            if (Q.n_elem == 0)
            {
                continue;
            }
            P = P * Q;
        }
        const arma::mat full = post.fullCoefficients(d, -1);
        if (full.n_cols != data.x.n_cols)
        {
            throw std::runtime_error(QObject::tr("The coefficients and the regressors of this file do not "
                                                 "match.").toStdString());
        }
        arma::mat y = data.y;
        arma::mat x = data.x;
        if (completed.rows > 0)
        {
            for (arma::uword t = 0; t < tt; ++t)
            {
                for (arma::uword i = 0; i < ku; ++i)
                {
                    y(t, i) = completed.at(t * ku + i, static_cast<std::size_t>(d));
                }
            }
            for (arma::uword t = 0; t < tt; ++t)
            {
                for (arma::uword l = 1; l <= static_cast<arma::uword>(p) && l <= t; ++l)
                {
                    x.row(t).cols((l - 1) * ku, l * ku - 1) = y.row(t - l);
                }
            }
        }
        dataMean += y.col(r);
        const arma::mat u = y - x * full.t();
        const arma::mat e = u * arma::trans(arma::inv(P));
        const arma::mat aLag = full.cols(0, ku * static_cast<arma::uword>(p) - 1);
        arma::mat history(ku * static_cast<arma::uword>(p), ku, arma::fill::zeros);   // M_{t-1} over M_{t-p}
        arma::mat one(tt, ku + 1, arma::fill::zeros);
        for (arma::uword t = 0; t < tt; ++t)
        {
            const arma::mat mt = aLag * history + P * arma::diagmat(e.row(t));
            one.row(t).cols(0, ku - 1) = mt.row(r);
            if (p > 1)
            {
                history.rows(ku, history.n_rows - 1) = arma::mat(history.rows(0, history.n_rows - ku - 1));
            }
            history.rows(0, ku - 1) = mt;
        }
        one.col(ku) = y.col(r) - arma::sum(one.cols(0, ku - 1), 1);
        draws.push_back(std::move(one));
    }
    if (draws.empty())
    {
        throw std::runtime_error(QObject::tr("No draw has an impact matrix to decompose with.").toStdString());
    }

    HistoricalDecomposition out;
    out.draws = static_cast<int>(draws.size());
    // The data, or, where some were not observed, their completion's mean.
    out.data = completed.rows > 0 ? arma::vec(dataMean / static_cast<double>(draws.size())) : arma::vec(data.y.col(r));
    out.centre.set_size(tt, ku + 1);
    out.lower.set_size(tt, ku + 1);
    out.upper.set_size(tt, ku + 1);
    std::vector<double> v(draws.size());
    for (arma::uword t = 0; t < tt; ++t)
    {
        for (arma::uword c = 0; c <= ku; ++c)
        {
            double sum = 0.0;
            for (std::size_t d = 0; d < draws.size(); ++d)
            {
                v[d] = draws[d](t, c);
                sum += v[d];
            }
            std::sort(v.begin(), v.end());
            out.centre(t, c) = request.median ? quantile7(v, 0.5) : sum / static_cast<double>(v.size());
            out.lower(t, c) = quantile7(v, (1.0 - request.ci) / 2.0);
            out.upper(t, c) = quantile7(v, 1.0 - (1.0 - request.ci) / 2.0);
        }
    }
    const HdSupport support = hdSupport(file);
    for (arma::uword t = 0; t < tt; ++t)
    {
        out.time.push_back(support.start + static_cast<double>(t) / support.frequency);
    }
    return out;
}

} // namespace studio
