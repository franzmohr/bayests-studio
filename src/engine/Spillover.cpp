#include "engine/Spillover.h"

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

QString spilloverTypeName(SpilloverType type)
{
    switch (type)
    {
    case SpilloverType::Gir: return QObject::tr("Generalised (Diebold and Yilmaz 2012)");
    case SpilloverType::Oir: return QObject::tr("Orthogonalised (Choleski)");
    case SpilloverType::Sign: return QObject::tr("Sign restrictions");
    }
    return {};
}

namespace
{

using detail::Posterior;
using detail::System;

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

/// bvartools' refusals in spillover.bvarmodel(), less the VEC one: a VEC is
/// rewritten as its level VAR here, which bvartools asks of vec_to_var().
void checkModel(const Posterior &post)
{
    if (post.structural())
    {
        throw std::runtime_error(
            QObject::tr("Spillover measures decompose the reduced form, which a structural model does not "
                        "store. Estimate it without the structural form to obtain them.").toStdString());
    }
    if (post.lagOrder() == 0)
    {
        throw std::runtime_error(QObject::tr("Spillover measures need a model with lags.").toStdString());
    }
    if (post.k() < 2)
    {
        throw std::runtime_error(
            QObject::tr("Spillover measures need at least two endogenous variables.").toStdString());
    }
}

/// The period a sign identification's rotations belong to; another is
/// refused where the model moves, as bvartools' .sign_period() refuses it.
int signPeriod(const QString &file, const Posterior &post, int requested)
{
    const auto stored = readSignIdentification(file);
    if (!stored)
    {
        throw std::runtime_error(
            QObject::tr("The model carries no sign restricted identification yet.").toStdString());
    }
    const int period = stored->period > 0 ? stored->period : post.periods();
    if (post.moving() && requested > 0 && requested != period)
    {
        throw std::runtime_error(QObject::tr("The rotations were found at period %1, and this model moves; "
                                             "they do not identify period %2.")
                                     .arg(period)
                                     .arg(requested)
                                     .toStdString());
    }
    return period;
}

/// One draw's table, or empty where a sign identification found no rotation.
arma::mat drawTable(const QString &file, const Posterior &post, int d, int period, const SpilloverRequest &request)
{
    const System s = post.system(d, period);
    arma::mat P;
    switch (request.type)
    {
    case SpilloverType::Gir: P = s.Sigma; break;
    case SpilloverType::Oir: P = arma::trans(arma::chol(s.Sigma)); break;
    case SpilloverType::Sign:
    {
        const arma::mat Q = post.rotation(file, d);
        if (Q.n_elem == 0)
        {
            return {};
        }
        P = arma::trans(arma::chol(s.Sigma)) * Q;
        break;
    }
    }
    return connectednessTable(s.A, s.Sigma, P, request.horizon, request.type == SpilloverType::Gir);
}

double sortedQuantile(const std::vector<double> &v, double q)
{
    // R's type 7, the default of the quantile() bvartools summarises with.
    const double h = (static_cast<double>(v.size()) - 1.0) * q;
    const auto lo = static_cast<std::size_t>(std::floor(h));
    const std::size_t hi = std::min(lo + 1, v.size() - 1);
    return v[lo] + (h - static_cast<double>(lo)) * (v[hi] - v[lo]);
}

} // namespace

SpilloverSupport spilloverSupport(const QString &file)
{
    const std::lock_guard<std::recursive_mutex> hdf5(hdf5Mutex());
    SpilloverSupport out;
    try
    {
        const Posterior post(file);
        checkModel(post);
        out.ok = true;
        out.moving = post.moving();
        out.periods = post.periods();
        out.k = post.k();
        out.variables = levelNames(file);
        for (int i = static_cast<int>(out.variables.size()); i < out.k; ++i)
        {
            out.variables << QObject::tr("Variable %1").arg(i + 1);
        }
        if (const auto id = readSignIdentification(file))
        {
            out.sign = true;
            out.signPeriod = id->period > 0 ? id->period : post.periods();
        }
        try
        {
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
        catch (const std::exception &)
        {
            // No time index: periods are counted 1, 2, ...
        }
    }
    catch (const std::exception &e)
    {
        out.reason = QString::fromUtf8(e.what());
    }
    return out;
}

arma::mat connectednessTable(const arma::mat &A, const arma::mat &Sigma, const arma::mat &P, int horizon,
                             bool generalised)
{
    if (horizon < 1)
    {
        throw std::runtime_error(QObject::tr("The spillover horizon must be at least one.").toStdString());
    }
    const arma::uword k = A.n_rows;
    const int lags = static_cast<int>(A.n_cols / k);

    // Phi_0 .. Phi_{H-1}, accumulated as they come: the H step forecast error
    // is made of the responses of periods 0 to H - 1.
    std::vector<arma::mat> phi{arma::eye<arma::mat>(k, k)};
    arma::mat numerator = arma::square(P);
    arma::vec mse = Sigma.diag();
    for (int i = 1; i < horizon; ++i)
    {
        arma::mat next(k, k, arma::fill::zeros);
        for (int j = 1; j <= std::min(i, lags); ++j)
        {
            next += phi[static_cast<std::size_t>(i - j)] *
                    A.cols(static_cast<arma::uword>(j - 1) * k, static_cast<arma::uword>(j) * k - 1);
        }
        phi.push_back(next);
        numerator += arma::square(next * P);
        mse += arma::mat(next * Sigma * arma::trans(next)).diag();
    }

    arma::mat theta = numerator;
    theta.each_col() /= mse;
    if (generalised)
    {
        // Pesaran and Shin: by the variance of the shock's variable.
        theta.each_row() /= arma::trans(Sigma.diag());
    }
    theta.each_col() /= arma::sum(theta, 1);
    return theta;
}

Spillovers spillovers(const QString &file, const SpilloverRequest &request)
{
    const Posterior post(file);
    checkModel(post);
    const int period =
        request.type == SpilloverType::Sign ? signPeriod(file, post, request.period) : request.period;
    const int k = post.k();
    const double kd = static_cast<double>(k);

    std::vector<arma::mat> tables;
    for (const int d : drawSubset(post.draws(), request.maxDraws))
    {
        arma::mat theta = drawTable(file, post, d, period, request);
        if (theta.n_elem != 0)
        {
            tables.push_back(std::move(theta));
        }
    }
    if (tables.empty())
    {
        throw std::runtime_error(QObject::tr("No draw is identified.").toStdString());
    }

    Spillovers out;
    out.period = period > 0 && period <= post.periods() ? period : post.periods();
    const auto n = static_cast<arma::uword>(tables.size());
    const auto ku = static_cast<arma::uword>(k);
    out.total.set_size(n);
    out.from.set_size(n, ku);
    out.to.set_size(n, ku);
    out.table.zeros(ku, ku);
    out.pairwise.zeros(ku, ku);
    for (arma::uword i = 0; i < n; ++i)
    {
        const arma::mat &theta = tables[i];
        const arma::vec own = theta.diag();
        out.from.row(i) = arma::trans(100.0 * (1.0 - own) / kd);
        out.to.row(i) = 100.0 * (arma::sum(theta, 0) - arma::trans(own)) / kd;
        // Off the table rather than as the sum of `from`, as bvartools, so the
        // identity sum(from) == sum(to) == total stays a check.
        out.total(i) = 100.0 * (arma::accu(theta) - arma::accu(own)) / kd;
        out.table += theta;
        out.pairwise += 100.0 * (arma::trans(theta) - theta) / kd;
    }
    out.net = out.to - out.from;
    out.table *= 100.0 / static_cast<double>(n);
    out.pairwise /= static_cast<double>(n);
    return out;
}

SpilloverPath spilloverPath(const QString &file, const SpilloverRequest &request,
                            const std::function<bool(int, int)> &progress)
{
    if (request.type == SpilloverType::Sign)
    {
        throw std::runtime_error(QObject::tr("A sign identification's rotations identify the period they were "
                                             "found in only.").toStdString());
    }
    const Posterior post(file);
    checkModel(post);
    if (!post.moving())
    {
        throw std::runtime_error(QObject::tr("Neither the coefficients nor the covariance of this model move, "
                                             "so neither does the index.").toStdString());
    }
    const SpilloverSupport support = spilloverSupport(file);
    const int periods = post.periods();
    const std::vector<int> pick = drawSubset(post.draws(), request.maxDraws);
    const double kd = static_cast<double>(post.k());
    const double probs[] = {0.05, 0.16, 0.5, 0.84, 0.95};

    SpilloverPath out;
    out.draws = static_cast<int>(pick.size());
    out.quantiles.set_size(static_cast<arma::uword>(periods), 5);
    std::vector<double> total(pick.size());
    for (int t = 1; t <= periods; ++t)
    {
        for (std::size_t i = 0; i < pick.size(); ++i)
        {
            const arma::mat theta = drawTable(file, post, pick[i], t, request);
            total[i] = 100.0 * (arma::accu(theta) - arma::trace(theta)) / kd;
        }
        std::sort(total.begin(), total.end());
        for (arma::uword q = 0; q < 5; ++q)
        {
            out.quantiles(static_cast<arma::uword>(t - 1), q) = sortedQuantile(total, probs[q]);
        }
        out.time.push_back(support.start + (t - 1) / support.frequency);
        if (progress && !progress(t, periods))
        {
            throw std::runtime_error(QObject::tr("Cancelled.").toStdString());
        }
    }
    return out;
}

} // namespace studio
