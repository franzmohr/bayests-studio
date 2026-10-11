#include "engine/Multipliers.h"

#include "engine/ModelFileTree.h"
#include "engine/PosteriorDraws.h"
#include "engine/Structural.h"

#include <QObject>

#include <highfive/highfive.hpp>

#include <algorithm>
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

/// bvartools' refusals in multipliers.bvarmodel() and multipliers.bvecmodel().
void checkModel(const QString &file, const Posterior &post)
{
    if (post.exogenous() == 0)
    {
        throw std::runtime_error(QObject::tr("Dynamic multipliers need a model with weakly exogenous variables, "
                                             "and this model has none.").toStdString());
    }
    if (post.structural())
    {
        // The coefficients are the structural ones and the recursion is the
        // reduced form's.
        throw std::runtime_error(
            QObject::tr("Dynamic multipliers are not available for a structural model.").toStdString());
    }
    if (post.vec() && post.exogenousLags() < 1)
    {
        throw std::runtime_error(QObject::tr("A VEC without differenced exogenous regressors has no level form "
                                             "to take the multipliers of.").toStdString());
    }
    // A grid of quantiles stacks the levels' draws.
    if (readGroupInt(file, QStringLiteral("/model"), QStringLiteral("quantiles"), -1) != -1)
    {
        throw std::runtime_error(QObject::tr("A grid of quantiles stacks the draws of every level.").toStdString());
    }
}

} // namespace

MultiplierSupport multiplierSupport(const QString &file)
{
    const std::lock_guard<std::recursive_mutex> hdf5(hdf5Mutex());
    MultiplierSupport out;
    try
    {
        const Posterior post(file);
        checkModel(file, post);
        out.ok = true;
        out.k = post.k();
        out.m = post.exogenous();
        out.moving = post.moving();
        out.periods = post.periods();
        out.variables = levelNames(file);
        for (int i = static_cast<int>(out.variables.size()); i < out.k; ++i)
        {
            out.variables << QObject::tr("Variable %1").arg(i + 1);
        }
        // bvartools keeps the names on /model/exogen, the builder on z.
        out.exogenous = readStringsAttribute(file, QStringLiteral("/data/train/z"), QStringLiteral("exogenous"));
        if (out.exogenous.size() != out.m)
        {
            try
            {
                const HighFive::File h5(file.toStdString(), HighFive::File::ReadOnly);
                const HighFive::Group model = h5.getGroup("/model");
                if (model.hasAttribute("exogen"))
                {
                    out.exogenous.clear();
                    for (const auto &n : model.getAttribute("exogen").read<std::vector<std::string>>())
                    {
                        out.exogenous << QString::fromStdString(n);
                    }
                }
            }
            catch (const std::exception &)
            {
            }
        }
        if (out.exogenous.size() != out.m)
        {
            out.exogenous.clear();
            for (int j = 0; j < out.m; ++j)
            {
                out.exogenous << QObject::tr("x%1").arg(j + 1);
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

std::vector<arma::mat> dynamicMultipliers(const QString &file, const MultiplierRequest &request)
{
    const Posterior post(file);
    checkModel(file, post);
    const int m = post.exogenous();
    const int s = post.exogenousLags();
    if (request.impulse < 0 || request.impulse >= m)
    {
        throw std::runtime_error(QObject::tr("No such exogenous variable.").toStdString());
    }
    if (request.horizon < 0)
    {
        throw std::runtime_error(QObject::tr("The horizon must be at least zero.").toStdString());
    }
    const auto k = static_cast<arma::uword>(post.k());
    const auto mu = static_cast<arma::uword>(m);
    const int horizons = request.horizon + 1;
    std::vector<arma::mat> out;
    for (const int d : drawSubset(post.draws(), request.maxDraws))
    {
        const arma::mat A = post.system(d, request.period).A;    // k x kp, the level form
        const arma::mat B = post.exogenousLevels(d, request.period);
        const int p = static_cast<int>(A.n_cols / k);
        // The column of each B_j that belongs to the variable moved.
        std::vector<arma::vec> b;
        for (int j = 0; j <= s; ++j)
        {
            b.push_back(B.col(static_cast<arma::uword>(j) * mu + static_cast<arma::uword>(request.impulse)));
        }
        arma::mat path(static_cast<arma::uword>(horizons), k, arma::fill::zeros);
        for (int h = 0; h < horizons; ++h)
        {
            arma::vec value(k, arma::fill::zeros);
            for (int l = 1; l <= std::min(h, p); ++l)
            {
                value += A.cols(static_cast<arma::uword>(l - 1) * k, static_cast<arma::uword>(l) * k - 1) *
                         path.row(static_cast<arma::uword>(h - l)).t();
            }
            if (request.permanent)
            {
                for (int j = 0; j <= std::min(h, s); ++j)
                {
                    value += b[static_cast<std::size_t>(j)];
                }
            }
            else if (h <= s)
            {
                value += b[static_cast<std::size_t>(h)];
            }
            path.row(static_cast<arma::uword>(h)) = value.t();
        }
        path *= request.shock;
        if (request.cumulative)
        {
            path = arma::cumsum(path, 0);
        }
        out.push_back(std::move(path));
    }
    return out;
}

} // namespace studio
