// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Franz X. Mohr

#include "engine/Structural.h"

#include "engine/ModelFileTree.h"
#include "engine/ModelRunner.h"
#include "engine/PosteriorDraws.h"

#include <QObject>

#include <highfive/highfive.hpp>

#include "io/hdf5/hdf5_and_armadillo.h"

#include <random>

#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>

namespace studio
{

QString responseTypeName(ResponseType type)
{
    switch (type)
    {
    case ResponseType::Feir: return QObject::tr("Forecast error (feir)");
    case ResponseType::Oir: return QObject::tr("Orthogonalised (oir)");
    case ResponseType::Gir: return QObject::tr("Generalised (gir)");
    case ResponseType::Sir: return QObject::tr("Structural (sir)");
    case ResponseType::Sgir: return QObject::tr("Structural generalised (sgir)");
    case ResponseType::Sign: return QObject::tr("Sign restrictions");
    }
    return {};
}

namespace
{

using detail::Posterior;
using detail::System;

/// Phi_0 .. Phi_h of the lag polynomial A (k x kp): Phi_i = sum_j Phi_{i-j} A_j.
std::vector<arma::mat> movingAverage(const arma::mat &A, int horizon)
{
    const arma::uword k = A.n_rows;
    const int p = static_cast<int>(A.n_cols / k);
    std::vector<arma::mat> phi;
    phi.push_back(arma::eye<arma::mat>(k, k));
    for (int i = 1; i <= horizon; ++i)
    {
        arma::mat next(k, k, arma::fill::zeros);
        for (int j = 1; j <= std::min(i, p); ++j)
        {
            next += phi[static_cast<std::size_t>(i - j)] *
                    A.cols(static_cast<arma::uword>((j - 1)) * k, static_cast<arma::uword>(j) * k - 1);
        }
        phi.push_back(next);
    }
    return phi;
}

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

/// bvartools' .refuse_unrotatable(): the rotation acts on the Choleski factor
/// of the reduced form covariance, which a structural model and a model that
/// never estimated covariances do not have. Empty where rotating is allowed.
QString unrotatable(const Posterior &post)
{
    if (post.structural())
    {
        return QObject::tr("A structural model has spent its identification on A_0; sign restrictions "
                           "need the reduced form. Estimate it without the structural form.");
    }
    const QString e = post.errorSpec();
    const bool covariances = e == QLatin1String("wishart") || e.endsWith(QLatin1String("+covar")) ||
                             e.isEmpty();
    if (!covariances)
    {
        return QObject::tr("Sign restrictions need estimated error covariances: Wishart errors, or a "
                           "covariance block with gamma or stochastic volatility errors.");
    }
    return {};
}

void checkType(const Posterior &post, ResponseType type)
{
    if (type == ResponseType::Sign)
    {
        if (const QString why = unrotatable(post); !why.isEmpty())
        {
            throw std::runtime_error(why.toStdString());
        }
        return;
    }
    const bool structuralType = type == ResponseType::Sir || type == ResponseType::Sgir;
    if (structuralType && !post.structural())
    {
        throw std::runtime_error(QObject::tr("Structural responses need a structural model.").toStdString());
    }
    if (!structuralType && post.structural())
    {
        // bvartools' irf(): the recursion would run on the structural
        // coefficients and the covariance of the structural errors.
        throw std::runtime_error(
            QObject::tr("A structural model's responses are sir or sgir: the others would be computed "
                        "from the structural coefficients rather than the reduced form.").toStdString());
    }
}

} // namespace

QStringList levelNames(const QString &file)
{
    QStringList names = readStringsAttribute(file, QStringLiteral("/data/train/y"), QStringLiteral("variables"));
    bool differenced = !names.isEmpty();
    for (const QString &n : names)
    {
        differenced = differenced && n.startsWith(QLatin1String("d."));
    }
    if (differenced)
    {
        for (QString &n : names)
        {
            n = n.mid(2);
        }
    }
    return names;
}

StructuralSupport structuralSupport(const QString &file)
{
    const std::lock_guard<std::recursive_mutex> hdf5(hdf5Mutex());
    StructuralSupport out;
    try
    {
        const Posterior post(file);
        out.ok = true;
        out.structural = post.structural();
        out.moving = post.moving();
        out.periods = post.periods();
        out.k = post.k();
        out.variables = levelNames(file);
        for (int i = static_cast<int>(out.variables.size()); i < out.k; ++i)
        {
            out.variables << QObject::tr("Variable %1").arg(i + 1);
        }
        out.signRefusal = unrotatable(post);
        out.signAllowed = out.signRefusal.isEmpty();
        out.sign = readSignIdentification(file);
    }
    catch (const std::exception &e)
    {
        out.reason = QString::fromUtf8(e.what());
    }
    return out;
}

namespace
{

/// The period a sign identification is read at: the one its rotations were
/// found in. bvartools' .sign_period() refuses another where Sigma moves,
/// since a rotation that satisfies the signs against one period's covariance
/// need not against another's.
int signPeriod(const QString &file, const ResponseRequest &request, const Posterior &post)
{
    const auto stored = readSignIdentification(file);
    if (!stored)
    {
        throw std::runtime_error(
            QObject::tr("The model carries no sign restricted identification yet.").toStdString());
    }
    const int storedPeriod = stored->period > 0 ? stored->period : post.periods();
    if (post.moving() && request.period > 0 && request.period != storedPeriod)
    {
        throw std::runtime_error(QObject::tr("The rotations were found at period %1, and this model's "
                                             "covariance moves; they do not identify period %2.")
                                     .arg(storedPeriod)
                                     .arg(request.period)
                                     .toStdString());
    }
    return storedPeriod;
}

/// The impact matrix of draw `d`, or empty where the draw is not identified.
arma::mat impact(const QString &file, const Posterior &post, const System &s, int d, ResponseType type,
                 arma::uword j)
{
    const arma::uword k = s.A.n_rows;
    switch (type)
    {
    case ResponseType::Feir: return arma::eye<arma::mat>(k, k);
    case ResponseType::Oir: return arma::trans(arma::chol(s.Sigma));
    case ResponseType::Gir: return s.Sigma / std::sqrt(s.Sigma(j, j));
    case ResponseType::Sir: return arma::inv(s.A0);
    case ResponseType::Sgir: return arma::solve(s.A0, s.Sigma) / std::sqrt(s.Sigma(j, j));
    case ResponseType::Sign:
    {
        const arma::mat Q = post.rotation(file, d);
        return Q.n_elem == 0 ? arma::mat() : arma::mat(arma::trans(arma::chol(s.Sigma)) * Q);
    }
    }
    return arma::eye<arma::mat>(k, k);
}

} // namespace

std::vector<arma::mat> impulseResponses(const QString &file, const ResponseRequest &request, int impulse)
{
    const Posterior post(file);
    checkType(post, request.type);
    const int period = request.type == ResponseType::Sign ? signPeriod(file, request, post) : request.period;
    const auto j = static_cast<arma::uword>(impulse);
    std::vector<arma::mat> out;
    for (const int d : drawSubset(post.draws(), request.maxDraws))
    {
        const System s = post.system(d, period);
        const arma::uword k = s.A.n_rows;
        arma::mat P = impact(file, post, s, d, request.type, j);
        if (P.n_elem == 0)
        {
            continue;   // no rotation was found for this draw
        }
        // feir and sir count a shock in units of the error unless asked for
        // standard deviations, as bvartools' shock = "sd".
        if (request.standardDeviation && (request.type == ResponseType::Feir || request.type == ResponseType::Sir))
        {
            P *= std::sqrt(s.Sigma(j, j));
        }
        const std::vector<arma::mat> phi = movingAverage(s.A, request.horizon);
        arma::mat response(static_cast<arma::uword>(request.horizon + 1), k);
        for (int h = 0; h <= request.horizon; ++h)
        {
            response.row(static_cast<arma::uword>(h)) =
                arma::trans(arma::mat(phi[static_cast<std::size_t>(h)] * P).col(j));
        }
        if (request.cumulative)
        {
            response = arma::cumsum(response, 0);
        }
        out.push_back(response);
    }
    return out;
}

std::vector<arma::mat> varianceDecomposition(const QString &file, const ResponseRequest &request, int response)
{
    if (request.type != ResponseType::Oir && request.type != ResponseType::Sir && request.type != ResponseType::Sign)
    {
        throw std::runtime_error(
            QObject::tr("The variance decomposition is offered for orthogonal shocks (oir, sir, sign "
                        "restrictions), whose shares sum to one.").toStdString());
    }
    const Posterior post(file);
    checkType(post, request.type);
    const int period = request.type == ResponseType::Sign ? signPeriod(file, request, post) : request.period;
    const auto i = static_cast<arma::uword>(response);
    std::vector<arma::mat> out;
    for (const int d : drawSubset(post.draws(), request.maxDraws))
    {
        const System s = post.system(d, period);
        const arma::uword k = s.A.n_rows;
        // bvartools' vardecomp(): P = chol(Sigma)' for oir, A_0^-1 chol(Sigma)'
        // for sir, with Sigma the structural errors' (diagonal) covariance, and
        // chol(Sigma)' Q for a sign restricted identification.
        arma::mat P = arma::trans(arma::chol(s.Sigma));
        if (request.type == ResponseType::Sir)
        {
            P = arma::solve(s.A0, P);
        }
        if (request.type == ResponseType::Sign)
        {
            const arma::mat Q = post.rotation(file, d);
            if (Q.n_elem == 0)
            {
                continue;
            }
            P = P * Q;
        }
        const std::vector<arma::mat> phi = movingAverage(s.A, request.horizon);
        arma::mat shares(static_cast<arma::uword>(request.horizon + 1), k);
        arma::rowvec numerator(k, arma::fill::zeros);
        for (int h = 0; h <= request.horizon; ++h)
        {
            numerator += arma::square(arma::mat(phi[static_cast<std::size_t>(h)] * P).row(i));
            shares.row(static_cast<arma::uword>(h)) = numerator / arma::accu(numerator);
        }
        out.push_back(shares);
    }
    return out;
}

std::optional<SignIdentification> readSignIdentification(const QString &file)
{
    const std::lock_guard<std::recursive_mutex> hdf5(hdf5Mutex());
    try
    {
        HighFive::File h5(file.toStdString(), HighFive::File::ReadOnly);
        if (!h5.exist("/model/sign_restrictions/restrictions") || !h5.exist("/posterior/q/coeffs"))
        {
            return std::nullopt;
        }
        SignIdentification id;
        // (4, n) in the dataspace: impulse, response, sign, horizon -- one
        // restriction per column, positions 1-based (bvartools' layout).
        const Matrix table = readMatrix(file, QStringLiteral("/model/sign_restrictions/restrictions"));
        if (table.rows != 4)
        {
            return std::nullopt;
        }
        for (std::size_t c = 0; c < table.cols; ++c)
        {
            SignRestriction r;
            r.impulse = static_cast<int>(table.at(0, c)) - 1;
            r.response = static_cast<int>(table.at(1, c)) - 1;
            // A sign and zero identification keeps its zeros here too.
            r.sign = table.at(2, c) > 0 ? 1 : (table.at(2, c) < 0 ? -1 : 0);
            // -1 stands for the long run, which only a sign and zero
            // identification restricts.
            r.horizon = std::isfinite(table.at(3, c)) ? static_cast<int>(table.at(3, c)) : -1;
            id.restrictions.push_back(r);
        }
        const HighFive::Group g = h5.getGroup("/model/sign_restrictions");
        const auto intAttribute = [&](const char *name, int fallback) {
            if (!g.hasAttribute(name))
            {
                return fallback;
            }
            const auto values = g.getAttribute(name).read<std::vector<double>>();
            return values.empty() ? fallback : static_cast<int>(values.front());
        };
        id.maxTries = intAttribute("max_tries", 1000);
        id.period = intAttribute("period", -1);
        id.rotated = intAttribute("rotated", 0);
        if (g.hasAttribute("thresholds"))
        {
            const auto thresholds = g.getAttribute("thresholds").read<std::vector<double>>();
            for (std::size_t r = 0; r < id.restrictions.size() && r < thresholds.size(); ++r)
            {
                id.restrictions[r].threshold = thresholds[r];
            }
        }
        const Matrix q = readMatrix(file, QStringLiteral("/posterior/q/coeffs"));
        id.draws = static_cast<int>(q.cols);
        for (std::size_t d = 0; d < q.cols; ++d)
        {
            id.accepted += std::isnan(q.at(0, d)) ? 0 : 1;
        }
        return id;
    }
    catch (const std::exception &)
    {
        return std::nullopt;
    }
}

SignIdentification identifySigns(const QString &file, const SignIdentification &spec, std::uint64_t seed,
                                 const std::function<bool(int, int)> &progress)
{
    if (spec.restrictions.empty())
    {
        throw std::runtime_error(QObject::tr("Give at least one sign restriction.").toStdString());
    }
    const Posterior post(file);
    checkType(post, ResponseType::Sign);
    const int k = post.k();
    int hMax = 0;
    for (const SignRestriction &r : spec.restrictions)
    {
        if (r.impulse < 0 || r.impulse >= k || r.response < 0 || r.response >= k || r.horizon < 0 ||
            (r.sign != 1 && r.sign != -1) || !(r.threshold >= 0.0))
        {
            throw std::runtime_error(QObject::tr("A restriction names a variable, a sign, a horizon or a "
                                                 "threshold that does not exist.").toStdString());
        }
        hMax = std::max(hMax, r.horizon);
    }
    if (spec.rotated < 0 || spec.rotated > k || spec.rotated == 1)
    {
        throw std::runtime_error(QObject::tr("Rotate two shocks or more, or all of them; one shock alone has "
                                             "no rotation but its sign.").toStdString());
    }
    const int block = spec.rotated > 0 ? spec.rotated : k;
    for (const SignRestriction &r : spec.restrictions)
    {
        if (r.impulse >= block)
        {
            throw std::runtime_error(QObject::tr("The %1 shock is restricted but not among the %2 rotated: "
                                                 "outside them the shocks are the Cholesky factor's, which "
                                                 "no rotation changes.")
                                         .arg(r.impulse + 1)
                                         .arg(block)
                                         .toStdString());
        }
    }
    // bvartools' .check_sign_restrictions(): both signs of one response at
    // one horizon reject every rotation, and would only after the whole budget.
    for (std::size_t a = 0; a < spec.restrictions.size(); ++a)
    {
        for (std::size_t b = a + 1; b < spec.restrictions.size(); ++b)
        {
            const SignRestriction &x = spec.restrictions[a];
            const SignRestriction &y = spec.restrictions[b];
            if (x.impulse == y.impulse && x.response == y.response && x.horizon == y.horizon && x.sign != y.sign)
            {
                throw std::runtime_error(QObject::tr("Two restrictions ask for both signs of the same "
                                                     "response at the same horizon.").toStdString());
            }
        }
    }
    std::vector<int> shocks;
    for (const SignRestriction &r : spec.restrictions)
    {
        if (std::find(shocks.begin(), shocks.end(), r.impulse) == shocks.end())
        {
            shocks.push_back(r.impulse);
        }
    }

    const int period = spec.period > 0 ? spec.period : -1;
    const auto kk = static_cast<arma::uword>(k);
    const int draws = post.draws();
    arma::mat q(kk * kk, static_cast<arma::uword>(draws), arma::fill::value(arma::datum::nan));
    std::mt19937_64 rng(seed);
    std::normal_distribution<double> normal(0.0, 1.0);
    int accepted = 0;
    for (int d = 0; d < draws; ++d)
    {
        if (progress && (d % 50 == 0) && !progress(d, draws))
        {
            throw std::runtime_error(QObject::tr("Cancelled.").toStdString());
        }
        const System s = post.system(d, period);
        const arma::mat P = arma::trans(arma::chol(s.Sigma));
        const std::vector<arma::mat> phi = movingAverage(s.A, hMax);
        std::vector<arma::mat> theta;
        for (const arma::mat &f : phi)
        {
            theta.push_back(f * P);
        }
        // Uniform on the orthogonal group of the rotated block, the identity
        // outside it.
        const auto nb = static_cast<arma::uword>(block);
        arma::mat draw(nb, nb), qFactor, rFactor;
        for (int attempt = 0; attempt < spec.maxTries; ++attempt)
        {
            draw.imbue([&]() { return normal(rng); });
            if (!arma::qr(qFactor, rFactor, draw))
            {
                continue;
            }
            arma::vec diagSign = arma::sign(rFactor.diag());
            diagSign.replace(0.0, 1.0);
            arma::mat candidate = arma::eye<arma::mat>(kk, kk);
            candidate.submat(0, 0, nb - 1, nb - 1) = qFactor * arma::diagmat(diagSign);
            bool ok = true;
            for (std::size_t si = 0; si < shocks.size() && ok; ++si)
            {
                const auto j = static_cast<arma::uword>(shocks[si]);
                bool asDrawn = true;
                bool flipped = true;
                for (const SignRestriction &r : spec.restrictions)
                {
                    if (r.impulse != shocks[si])
                    {
                        continue;
                    }
                    const double value = arma::dot(theta[static_cast<std::size_t>(r.horizon)].row(
                                                       static_cast<arma::uword>(r.response)),
                                                   candidate.col(j));
                    // Beyond the threshold on the side of the sign; flipped,
                    // the column satisfies it on the other.
                    if (r.sign > 0)
                    {
                        asDrawn = asDrawn && value > r.threshold;
                        flipped = flipped && value < -r.threshold;
                    }
                    else
                    {
                        asDrawn = asDrawn && value < -r.threshold;
                        flipped = flipped && value > r.threshold;
                    }
                }
                if (!asDrawn)
                {
                    if (flipped)
                    {
                        candidate.col(j) *= -1.0;
                    }
                    else
                    {
                        ok = false;
                    }
                }
            }
            if (ok)
            {
                q.col(static_cast<arma::uword>(d)) = arma::vectorise(candidate);
                ++accepted;
                break;
            }
        }
    }
    if (progress)
    {
        progress(draws, draws);
    }
    if (accepted == 0)
    {
        throw std::runtime_error(QObject::tr("No rotation satisfying the restrictions was found for any "
                                             "of the %1 draws within %2 tries each. The restrictions "
                                             "contradict each other, or the model does not produce the "
                                             "pattern they describe.")
                                     .arg(draws)
                                     .arg(spec.maxTries)
                                     .toStdString());
    }

    // Written as bvartools' write_to_hdf5() writes it, so that R reads the
    // identification back with the model and irf(type = "sign") uses it.
    HighFive::File h5(file.toStdString(), HighFive::File::ReadWrite);
    const ModelFile mf(h5);
    if (h5.exist("/posterior/q"))
    {
        h5.unlink("/posterior/q");
    }
    if (h5.exist("/model/sign_restrictions"))
    {
        h5.unlink("/model/sign_restrictions");
    }
    h5.createGroup("/posterior/q");
    write_armadillo_matrix_to_hdf5(mf, "/posterior/q/coeffs", arma::mat(q.t()), true);   // (k*k, draws) on disk
    HighFive::Group g = h5.createGroup("/model/sign_restrictions");
    arma::mat table(spec.restrictions.size(), 4);
    for (std::size_t r = 0; r < spec.restrictions.size(); ++r)
    {
        const SignRestriction &x = spec.restrictions[r];
        table(r, 0) = x.impulse + 1;
        table(r, 1) = x.response + 1;
        table(r, 2) = x.sign;
        table(r, 3) = x.horizon;
    }
    write_armadillo_matrix_to_hdf5(mf, "/model/sign_restrictions/restrictions", table, false);   // (4, n) on disk
    const std::vector<std::string> columns{"impulse", "response", "sign", "horizon"};
    h5.getDataSet("/model/sign_restrictions/restrictions").createAttribute("columns", columns);
    g.createAttribute("max_tries", std::vector<int>{spec.maxTries});
    // The studio's additions, which bvartools does not read: written only when
    // they say something, so a plain identification's file is bvartools' own.
    bool thresholds = false;
    std::vector<double> threshold;
    for (const SignRestriction &x : spec.restrictions)
    {
        threshold.push_back(x.threshold);
        thresholds = thresholds || x.threshold > 0.0;
    }
    if (thresholds)
    {
        g.createAttribute("thresholds", threshold);
    }
    if (spec.rotated > 0 && spec.rotated < k)
    {
        g.createAttribute("rotated", std::vector<int>{spec.rotated});
    }
    if (spec.period > 0)
    {
        g.createAttribute("period", std::vector<int>{spec.period});
    }

    SignIdentification out = spec;
    out.accepted = accepted;
    out.draws = draws;
    return out;
}

} // namespace studio
