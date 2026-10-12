// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Franz X. Mohr

#include "engine/SignZero.h"

#include "engine/PosteriorDraws.h"

#include <QFile>
#include <QObject>

#include <highfive/highfive.hpp>

#include "io/hdf5/hdf5_and_armadillo.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <random>
#include <set>
#include <stdexcept>

// Inference in an SVAR identified with sign and zero restrictions, after Arias,
// Rubio-Ramírez and Waggoner (2018), as bvartools implements it
// (src/arias_rubio_ramirez_waggoner_2018.cpp, R/add_sign_zero_restrictions.
// bvarmodel.R, R/arias_rubio_ramirez_waggoner_2018.R and
// src/pareto_smoothed_importance_sampling.cpp). The structure and the comments
// follow those files closely so the two can be read side by side; where this
// one differs it says so.

namespace studio
{

namespace
{

constexpr double kNegInf = -std::numeric_limits<double>::infinity();

/// Everything about the identification that does not change from draw to
/// draw. `w` completes the constraints on each column to a square system
/// (Appendix A.3): drawn once per run and fixed, because the volume element
/// differentiates the map it takes part in.
struct ArwSetup
{
    int k = 0;                       // endogenous variables
    int m = 0;                       // regressors per equation, deterministic terms included
    int lags = 0;
    std::vector<double> horizons;    // the horizons F stacks, +inf for the long run
    std::vector<arma::mat> z;        // k blocks, z_j x (nh * k): the zero restrictions
    std::vector<arma::mat> s;        // k blocks, s_j x (nh * k): the sign restrictions
    std::vector<arma::mat> w;        // k blocks, (k - (j - 1 + z_j)) x k
    int dim = 0;                     // the w blocks' rows together: where the spheres live
    int nzeros = 0;
};

/// F(A_0, A_+): the responses the restrictions are written on, one k x k block
/// per horizon, each transposed so that its jth column is the response to the
/// jth shock. Horizon zero is inv(A_0)', the long run inv(A_0 - A_1 - ... - A_p)'.
arma::mat stackedResponses(const arma::mat &a0, const arma::mat &aplus, const ArwSetup &setup)
{
    const int k = setup.k;
    const int nh = static_cast<int>(setup.horizons.size());
    arma::mat out(static_cast<arma::uword>(nh * k), static_cast<arma::uword>(k), arma::fill::zeros);
    if (nh == 0)
    {
        return out;
    }
    int maxFinite = 0;
    bool longRun = false;
    for (const double h : setup.horizons)
    {
        if (std::isinf(h))
        {
            longRun = true;
        }
        else
        {
            maxFinite = std::max(maxFinite, static_cast<int>(h));
        }
    }
    const auto ku = static_cast<arma::uword>(k);
    std::vector<arma::mat> a(static_cast<std::size_t>(setup.lags));
    for (int i = 0; i < setup.lags; ++i)
    {
        a[static_cast<std::size_t>(i)] = aplus.rows(static_cast<arma::uword>(i) * ku, static_cast<arma::uword>(i + 1) * ku - 1);
    }
    // R(i) = (R(0) A_i + R(1) A_{i-1} + ... + R(i-1) A_1) inv(A_0).
    std::vector<arma::mat> r(static_cast<std::size_t>(maxFinite + 1));
    const int first = std::min(maxFinite, setup.lags);
    for (int i = 1; i <= first; ++i)
    {
        arma::mat x = arma::solve(a0, a[static_cast<std::size_t>(i - 1)]);
        for (int j = 1; j < i; ++j)
        {
            x += r[static_cast<std::size_t>(j)] * a[static_cast<std::size_t>(i - j - 1)];
        }
        r[static_cast<std::size_t>(i)] = arma::solve(a0.t(), x.t()).t();
    }
    for (int i = setup.lags + 1; i <= maxFinite; ++i)
    {
        arma::mat x(ku, ku, arma::fill::zeros);
        if (setup.lags > 0)
        {
            x = r[static_cast<std::size_t>(i - setup.lags)] * a[static_cast<std::size_t>(setup.lags - 1)];
        }
        for (int j = 1; j < setup.lags; ++j)
        {
            x += r[static_cast<std::size_t>(i - j)] * a[static_cast<std::size_t>(j - 1)];
        }
        r[static_cast<std::size_t>(i)] = arma::solve(a0.t(), x.t()).t();
    }
    arma::mat rLongRun;
    if (longRun)
    {
        arma::mat total = a0;
        for (int j = 0; j < setup.lags; ++j)
        {
            total -= a[static_cast<std::size_t>(j)];
        }
        rLongRun = arma::inv(total);
    }
    for (int i = 0; i < nh; ++i)
    {
        const auto rows = arma::span(static_cast<arma::uword>(i) * ku, static_cast<arma::uword>(i + 1) * ku - 1);
        const double h = setup.horizons[static_cast<std::size_t>(i)];
        if (std::isinf(h))
        {
            out.rows(rows.a, rows.b) = rLongRun.t();
        }
        else if (h == 0.0)
        {
            out.rows(rows.a, rows.b) = arma::inv(a0).t();
        }
        else
        {
            out.rows(rows.a, rows.b) = r[static_cast<std::size_t>(h)].t();
        }
    }
    return out;
}

/// Z_j F, one block per shock: what makes the zero restrictions on column j
/// of Q linear.
std::vector<arma::mat> zfBlocks(const arma::mat &f, const ArwSetup &setup)
{
    std::vector<arma::mat> out(static_cast<std::size_t>(setup.k));
    for (int j = 0; j < setup.k; ++j)
    {
        out[static_cast<std::size_t>(j)] = setup.z[static_cast<std::size_t>(j)] * f;
    }
    return out;
}

/// An orthonormal basis for the null space of the constraints on column j,
/// signed so that it is a function of them rather than of a decomposition's
/// conventions.
bool nullBasis(const arma::mat &constraints, const arma::mat &fixed, arma::mat &basis)
{
    const arma::uword k = fixed.n_cols;
    const arma::uword s = fixed.n_rows;
    arma::mat stacked(k, k);
    if (constraints.n_rows > 0)
    {
        stacked.rows(0, constraints.n_rows - 1) = constraints;
    }
    stacked.rows(k - s, k - 1) = fixed;
    arma::mat qFactor, rFactor;
    if (!arma::qr(qFactor, rFactor, arma::mat(stacked.t())))
    {
        return false;
    }
    for (arma::uword i = k - s; i < k; ++i)
    {
        if (rFactor(i, i) < 0.0)
        {
            qFactor.col(i) = -qFactor.col(i);
        }
    }
    basis = qFactor.cols(k - s, k - 1);
    return true;
}

/// The constraints on column j: the columns already placed, then the zeros.
arma::mat columnConstraints(const arma::mat &q, int j, const arma::mat &zf)
{
    arma::mat out(static_cast<arma::uword>(j) + zf.n_rows, q.n_rows);
    if (j > 0)
    {
        out.rows(0, static_cast<arma::uword>(j) - 1) = q.cols(0, static_cast<arma::uword>(j) - 1).t();
    }
    if (zf.n_rows > 0)
    {
        out.rows(static_cast<arma::uword>(j), static_cast<arma::uword>(j) + zf.n_rows - 1) = zf;
    }
    return out;
}

/// Steps 2 and 3 of Algorithm 2: unit vectors become the columns of an
/// orthogonal matrix that satisfies the zero restrictions.
bool spheresToQ(const arma::vec &w, const std::vector<arma::mat> &zf, const ArwSetup &setup, arma::mat &q)
{
    const auto k = static_cast<arma::uword>(setup.k);
    q.zeros(k, k);
    arma::uword at = 0;
    for (int j = 0; j < setup.k; ++j)
    {
        const arma::uword s = setup.w[static_cast<std::size_t>(j)].n_rows;
        arma::mat basis;
        if (!nullBasis(columnConstraints(q, j, zf[static_cast<std::size_t>(j)]), setup.w[static_cast<std::size_t>(j)], basis))
        {
            return false;
        }
        q.col(static_cast<arma::uword>(j)) = basis * w.subvec(at, at + s - 1);
        at += s;
    }
    return true;
}

/// Its inverse, rebuilding the same basis column by column.
bool qToSpheres(const arma::mat &q, const std::vector<arma::mat> &zf, const ArwSetup &setup, arma::vec &w)
{
    w.zeros(static_cast<arma::uword>(setup.dim));
    arma::uword at = 0;
    for (int j = 0; j < setup.k; ++j)
    {
        const arma::uword s = setup.w[static_cast<std::size_t>(j)].n_rows;
        arma::mat basis;
        if (!nullBasis(columnConstraints(q, j, zf[static_cast<std::size_t>(j)]), setup.w[static_cast<std::size_t>(j)], basis))
        {
            return false;
        }
        w.subvec(at, at + s - 1) = basis.t() * q.col(static_cast<arma::uword>(j));
        at += s;
    }
    return true;
}

/// h(Sigma): upper triangular, h' h = Sigma, symmetrised first because the
/// numerical derivative walks off the symmetric matrices.
bool cholTilde(const arma::mat &sigma, arma::mat &out)
{
    return arma::chol(out, arma::mat(0.5 * (sigma + sigma.t())));
}

/// f_h^{-1}: (B, Sigma, Q) to (A_0, A_+).
bool orthogonalToStructural(const arma::mat &b, const arma::mat &sigma, const arma::mat &q, arma::mat &a0,
                            arma::mat &aplus)
{
    arma::mat h;
    if (!cholTilde(sigma, h))
    {
        return false;
    }
    a0 = arma::solve(arma::trimatu(h), q);
    aplus = b * a0;
    return true;
}

/// f_h, the other way.
bool structuralToOrthogonal(const arma::mat &a0, const arma::mat &aplus, arma::mat &b, arma::mat &sigma,
                            arma::mat &q)
{
    b = arma::solve(a0.t(), aplus.t()).t();
    const arma::mat gram = a0 * a0.t();
    if (!arma::inv_sympd(sigma, arma::mat(0.5 * (gram + gram.t()))))
    {
        return false;
    }
    arma::mat h;
    if (!cholTilde(sigma, h))
    {
        return false;
    }
    q = h * a0;
    return true;
}

/// The zero restrictions as a function of the structural parameters.
bool zeroRestrictions(const arma::vec &x, const ArwSetup &setup, arma::vec &out)
{
    const auto k = static_cast<arma::uword>(setup.k);
    const arma::mat a0 = arma::reshape(x.subvec(0, k * k - 1), k, k);
    const arma::mat aplus = arma::reshape(x.subvec(k * k, x.n_elem - 1), static_cast<arma::uword>(setup.m), k);
    const arma::mat f = stackedResponses(a0, aplus, setup);
    out.zeros(static_cast<arma::uword>(setup.nzeros));
    arma::uword at = 0;
    for (int j = 0; j < setup.k; ++j)
    {
        const arma::uword s = setup.z[static_cast<std::size_t>(j)].n_rows;
        if (s == 0)
        {
            continue;
        }
        out.subvec(at, at + s - 1) = setup.z[static_cast<std::size_t>(j)] * f.col(static_cast<arma::uword>(j));
        at += s;
    }
    return true;
}

/// ff_h: the structural parameters to (B, Sigma, w) -- Algorithm 2 read
/// backwards, whose volume element on the zero manifold Algorithm 3 divides by.
bool structuralToSpheres(const arma::vec &x, const ArwSetup &setup, arma::vec &out)
{
    const auto k = static_cast<arma::uword>(setup.k);
    const auto m = static_cast<arma::uword>(setup.m);
    const arma::mat a0 = arma::reshape(x.subvec(0, k * k - 1), k, k);
    const arma::mat aplus = arma::reshape(x.subvec(k * k, x.n_elem - 1), m, k);
    arma::mat b, sigma, q;
    if (!structuralToOrthogonal(a0, aplus, b, sigma, q))
    {
        return false;
    }
    arma::mat a0Identity, aplusIdentity;
    if (!orthogonalToStructural(b, sigma, arma::eye(k, k), a0Identity, aplusIdentity))
    {
        return false;
    }
    const std::vector<arma::mat> zf = zfBlocks(stackedResponses(a0Identity, aplusIdentity, setup), setup);
    arma::vec w;
    if (!qToSpheres(q, zf, setup, w))
    {
        return false;
    }
    out.set_size(m * k + k * k + static_cast<arma::uword>(setup.dim));
    out.subvec(0, m * k - 1) = arma::vectorise(b);
    out.subvec(m * k, m * k + k * k - 1) = arma::vectorise(sigma);
    if (setup.dim > 0)
    {
        out.subvec(m * k + k * k, out.n_elem - 1) = w;
    }
    return true;
}

template <typename Fn>
bool numericalDerivative(Fn fn, const arma::vec &x, double epsilon, bool oneSided, arma::mat &out)
{
    const arma::uword n = x.n_elem;
    arma::vec z = x;
    arma::vec base;
    if (oneSided && !fn(x, base))
    {
        return false;
    }
    for (arma::uword j = 0; j < n; ++j)
    {
        arma::vec high, low;
        z(j) = x(j) + epsilon;
        if (!fn(z, high))
        {
            return false;
        }
        if (oneSided)
        {
            low = base;
        }
        else
        {
            z(j) = x(j) - epsilon;
            if (!fn(z, low))
            {
                return false;
            }
        }
        z(j) = x(j);
        if (j == 0)
        {
            out.set_size(high.n_elem, n);
        }
        out.col(j) = (high - low) / (oneSided ? epsilon : 2.0 * epsilon);
    }
    return true;
}

/// The log of the volume element of ff_h on the manifold the zeros define
/// (Theorem 3), numerically. NaN where it cannot be computed.
double logVolumeElement(const ArwSetup &setup, const arma::vec &x, double epsilon, bool oneSided)
{
    arma::mat dFn;
    if (!numericalDerivative([&setup](const arma::vec &v, arma::vec &y) { return structuralToSpheres(v, setup, y); },
                             x, epsilon, oneSided, dFn))
    {
        return std::nan("");
    }
    arma::mat n;
    if (setup.nzeros > 0)
    {
        arma::mat dR;
        if (!numericalDerivative([&setup](const arma::vec &v, arma::vec &y) { return zeroRestrictions(v, setup, y); },
                                 x, epsilon, oneSided, dR))
        {
            return std::nan("");
        }
        const arma::mat basis = arma::null(dR);
        if (basis.n_cols == 0)
        {
            return std::nan("");
        }
        n = dFn * basis;
    }
    else
    {
        n = dFn;
    }
    const arma::mat gram = n.t() * n;
    double value = 0.0;
    if (!arma::log_det_sympd(value, arma::mat(0.5 * (gram + gram.t()))))
    {
        return std::nan("");
    }
    return 0.5 * value;
}

/// One draw's rotation and log weight (bvartools' arw_draw_q()). `coef` is
/// k x m with the deterministic terms included.
ArwDraw arwDraw(const arma::mat &coef, const arma::mat &sigma, const ArwSetup &setup, std::mt19937_64 &rng,
                bool oneSided, int maxTries, double epsilon = 1e-6)
{
    const auto k = static_cast<arma::uword>(setup.k);
    const auto m = static_cast<arma::uword>(setup.m);
    const arma::mat b = coef.t();   // the paper's B is regressors by equations
    ArwDraw failed;
    failed.logWeight = std::nan("");
    arma::mat a0Identity, aplusIdentity;
    if (!orthogonalToStructural(b, sigma, arma::eye(k, k), a0Identity, aplusIdentity))
    {
        return failed;
    }
    const std::vector<arma::mat> zf = zfBlocks(stackedResponses(a0Identity, aplusIdentity, setup), setup);
    std::normal_distribution<double> normal(0.0, 1.0);
    const bool flip = maxTries > 1;
    auto drawRotation = [&](arma::mat &q, arma::mat &a0, arma::mat &aplus) {
        arma::vec w(static_cast<arma::uword>(setup.dim));
        arma::uword at = 0;
        for (int j = 0; j < setup.k; ++j)
        {
            const arma::uword s = setup.w[static_cast<std::size_t>(j)].n_rows;
            arma::vec wj(s);
            wj.imbue([&]() { return normal(rng); });
            const double norm = arma::norm(wj);
            if (!(norm > 0.0))
            {
                return false;
            }
            w.subvec(at, at + s - 1) = wj / norm;
            at += s;
        }
        if (!spheresToQ(w, zf, setup, q) || !orthogonalToStructural(b, sigma, q, a0, aplus))
        {
            return false;
        }
        // Step 2 of Algorithm 3, with a column flipped where that is all it needs.
        const arma::mat f = stackedResponses(a0, aplus, setup);
        for (int j = 0; j < setup.k; ++j)
        {
            const arma::mat &sj = setup.s[static_cast<std::size_t>(j)];
            if (sj.n_rows == 0)
            {
                continue;
            }
            const arma::vec signs = sj * f.col(static_cast<arma::uword>(j));
            if (signs.min() > 0.0)
            {
                continue;
            }
            if (flip && signs.max() < 0.0)
            {
                q.col(static_cast<arma::uword>(j)) *= -1.0;
                a0.col(static_cast<arma::uword>(j)) *= -1.0;
                aplus.col(static_cast<arma::uword>(j)) *= -1.0;
                continue;
            }
            return false;
        }
        return true;
    };
    // Tries until the second success or maxTries, keeping the first: the
    // unbiased estimate of p (Girshick, Mosteller and Savage 1946).
    arma::mat q, a0, aplus;
    int successes = 0, tries = 0, secondAt = 0;
    while (tries < maxTries && successes < 2)
    {
        ++tries;
        arma::mat qTry, a0Try, aplusTry;
        if (drawRotation(qTry, a0Try, aplusTry))
        {
            ++successes;
            if (successes == 1)
            {
                q = qTry;
                a0 = a0Try;
                aplus = aplusTry;
            }
            else
            {
                secondAt = tries;
            }
        }
    }
    ArwDraw out;
    out.tries = tries;
    if (successes == 0)
    {
        out.logWeight = kNegInf;
        out.logPHat = kNegInf;
        return out;
    }
    out.logPHat = successes == 2 ? -std::log(static_cast<double>(secondAt - 1)) : -std::log(static_cast<double>(tries));
    out.q = q;
    arma::vec x(k * k + m * k);
    x.subvec(0, k * k - 1) = arma::vectorise(a0);
    x.subvec(k * k, x.n_elem - 1) = arma::vectorise(aplus);
    // The closed-form volume element of f_h (Proposition 1) against the
    // numerical one of Algorithm 2's construction; the prior cancels.
    double logAbsDet = 0.0, detSign = 0.0;
    if (!arma::log_det(logAbsDet, detSign, a0) || !std::isfinite(logAbsDet))
    {
        out.logWeight = std::nan("");
        return out;
    }
    const double kd = static_cast<double>(k);
    const double logVeFh = 0.5 * kd * (kd + 1.0) * std::log(2.0) - (2.0 * kd + static_cast<double>(m) + 1.0) * logAbsDet;
    const double logVeGfh = logVolumeElement(setup, x, epsilon, oneSided);
    out.logWeight = std::isfinite(logVeGfh) ? logVeFh - logVeGfh + out.logPHat : std::nan("");
    return out;
}

/// The restriction table as the worker wants it (bvartools' .arw_setup()).
ArwSetup arwSetup(const std::vector<ZeroSignRestriction> &restrictions, int k, int m, int lags,
                  std::mt19937_64 &rng)
{
    ArwSetup setup;
    setup.k = k;
    setup.m = m;
    setup.lags = lags;
    std::set<double> horizons;
    for (const auto &r : restrictions)
    {
        horizons.insert(r.horizon);   // +inf sorts last, as R's sort() puts it
    }
    setup.horizons.assign(horizons.begin(), horizons.end());
    const auto nh = static_cast<arma::uword>(setup.horizons.size());
    const auto ku = static_cast<arma::uword>(k);
    auto block = [&](int shock, bool zero) {
        std::vector<const ZeroSignRestriction *> rows;
        for (const auto &r : restrictions)
        {
            if (r.impulse == shock && ((r.sign == 0) == zero))
            {
                rows.push_back(&r);
            }
        }
        arma::mat out(rows.size(), nh * ku, arma::fill::zeros);
        for (std::size_t i = 0; i < rows.size(); ++i)
        {
            const auto h = static_cast<arma::uword>(
                std::find(setup.horizons.begin(), setup.horizons.end(), rows[i]->horizon) - setup.horizons.begin());
            out(i, h * ku + static_cast<arma::uword>(rows[i]->response)) = rows[i]->sign == 0 ? 1.0 : rows[i]->sign;
        }
        return out;
    };
    std::normal_distribution<double> normal(0.0, 1.0);
    for (int j = 0; j < k; ++j)
    {
        setup.z.push_back(block(j, true));
        setup.s.push_back(block(j, false));
        const auto rows = static_cast<arma::uword>(k - (j + static_cast<int>(setup.z.back().n_rows)));
        arma::mat w(rows, ku);
        w.imbue([&]() { return normal(rng); });
        setup.w.push_back(w);
        setup.dim += static_cast<int>(rows);
        setup.nzeros += static_cast<int>(setup.z.back().n_rows);
    }
    return setup;
}

double logSumExp(const std::vector<double> &x)
{
    double hi = kNegInf;
    for (const double v : x)
    {
        hi = std::max(hi, v);
    }
    if (!std::isfinite(hi))
    {
        return hi;
    }
    double total = 0.0;
    for (const double v : x)
    {
        total += std::exp(v - hi);
    }
    return hi + std::log(total);
}

double profileLoglik(double theta, const std::vector<double> &x)
{
    const double a = -theta;
    double k = 0.0;
    for (const double v : x)
    {
        k += std::log1p(a * v);
    }
    k /= static_cast<double>(x.size());
    const double ratio = a / k;
    if (!std::isfinite(ratio) || !(ratio > 0.0))
    {
        return kNegInf;
    }
    return std::log(ratio) - k - 1.0;
}

struct GpdFit
{
    double k = std::nan("");
    double sigma = std::nan("");
    bool ok = false;
};

/// Zhang and Stephens (2009), with loo's weakly informative prior on k.
GpdFit fitGeneralisedPareto(const std::vector<double> &x)
{
    GpdFit out;
    const std::size_t n = x.size();
    if (n < 5 || !(x.back() > 0.0))
    {
        return out;
    }
    const auto quarter = static_cast<std::size_t>(std::floor(static_cast<double>(n) / 4.0 + 0.5));
    const double xStar = x[quarter > 0 ? quarter - 1 : 0];
    if (!(xStar > 0.0))
    {
        return out;
    }
    const int grid = 30 + static_cast<int>(std::floor(std::sqrt(static_cast<double>(n))));
    std::vector<double> theta(static_cast<std::size_t>(grid)), logLik(static_cast<std::size_t>(grid));
    for (int j = 0; j < grid; ++j)
    {
        const double jj = static_cast<double>(j) + 1.0;
        theta[static_cast<std::size_t>(j)] =
            1.0 / x.back() + (1.0 - std::sqrt(static_cast<double>(grid) / (jj - 0.5))) / (3.0 * xStar);
        logLik[static_cast<std::size_t>(j)] = static_cast<double>(n) * profileLoglik(theta[static_cast<std::size_t>(j)], x);
    }
    const double normaliser = logSumExp(logLik);
    if (!std::isfinite(normaliser))
    {
        return out;
    }
    double thetaHat = 0.0;
    for (int j = 0; j < grid; ++j)
    {
        thetaHat += theta[static_cast<std::size_t>(j)] * std::exp(logLik[static_cast<std::size_t>(j)] - normaliser);
    }
    double k = 0.0;
    for (const double v : x)
    {
        k += std::log1p(-thetaHat * v);
    }
    k /= static_cast<double>(n);
    const double sigma = -k / thetaHat;
    const double a = 10.0;
    const double nPlusA = static_cast<double>(n) + a;
    out.k = k * static_cast<double>(n) / nPlusA + a * 0.5 / nPlusA;
    out.sigma = sigma;
    out.ok = std::isfinite(out.k) && std::isfinite(sigma) && sigma > 0.0;
    return out;
}

double generalisedParetoQuantile(double p, double k, double sigma)
{
    if (std::abs(k) < 1e-30)
    {
        return -sigma * std::log1p(-p);
    }
    return sigma * std::expm1(-k * std::log1p(-p)) / k;
}

/// bvartools' .arw_weights(): normalised weights and the Pareto shape.
std::vector<double> arwWeights(const std::vector<double> &logWeight, bool smooth, double &paretoK)
{
    paretoK = std::nan("");
    std::vector<int> finite;
    for (std::size_t i = 0; i < logWeight.size(); ++i)
    {
        if (std::isfinite(logWeight[i]))
        {
            finite.push_back(static_cast<int>(i));
        }
    }
    auto normalise = [&](const std::vector<double> &lw) {
        std::vector<double> weights(lw.size(), 0.0);
        double hi = kNegInf;
        for (const int i : finite)
        {
            hi = std::max(hi, lw[static_cast<std::size_t>(i)]);
        }
        double total = 0.0;
        for (const int i : finite)
        {
            weights[static_cast<std::size_t>(i)] = std::exp(lw[static_cast<std::size_t>(i)] - hi);
            total += weights[static_cast<std::size_t>(i)];
        }
        for (double &w : weights)
        {
            w /= total;
        }
        return weights;
    };
    if (!smooth || finite.size() < 25)
    {
        return normalise(logWeight);
    }
    double lo = std::numeric_limits<double>::infinity(), hi = kNegInf;
    for (const int i : finite)
    {
        lo = std::min(lo, logWeight[static_cast<std::size_t>(i)]);
        hi = std::max(hi, logWeight[static_cast<std::size_t>(i)]);
    }
    // Equal by construction (signs alone): no tail to fit.
    if (hi - lo < 1e-6)
    {
        std::vector<double> weights(logWeight.size(), 0.0);
        for (const int i : finite)
        {
            weights[static_cast<std::size_t>(i)] = 1.0 / static_cast<double>(finite.size());
        }
        return weights;
    }
    const std::vector<double> smoothed = psisSmooth(logWeight, paretoK);
    return normalise(smoothed);
}

/// bvartools' .warn_importance_sample().
QStringList importanceWarnings(int effective, double largest, int accepted, double paretoK)
{
    QStringList out;
    const bool tooFew = effective < 20;
    const bool wasteful = static_cast<double>(effective) / accepted < 0.25;
    const bool heavy = std::isfinite(paretoK) && paretoK >= 0.7;
    if (!tooFew && !wasteful && !heavy)
    {
        return out;
    }
    QStringList detail;
    if (heavy)
    {
        detail << QObject::tr("the shape of its weight tail is %1, above the 0.7 at which the estimator has "
                              "neither a finite variance nor a meaningful effective sample size")
                      .arg(paretoK, 0, 'f', 2);
    }
    if (tooFew)
    {
        detail << QObject::tr("its effective sample size is %1, too few to read percentiles from").arg(effective);
    }
    if (wasteful)
    {
        detail << QObject::tr("it keeps only %1% of the information in the %2 draws that satisfied the signs")
                      .arg(100.0 * effective / accepted, 0, 'f', 1)
                      .arg(accepted);
    }
    const QString arbitrary = QObject::tr(
        "The proposal is partly arbitrary, though -- the matrices completing each shock's constraints are "
        "drawn at random and any draw of them is valid -- so re-running under a different seed gives a "
        "different and possibly far more efficient importance sampler.");
    QString remedy;
    if (heavy)
    {
        remedy = QObject::tr("More candidate draws will not repair this: the proposal put too little probability "
                             "where the target has a lot.") + " " + arbitrary;
    }
    else if (!std::isfinite(paretoK) && largest * accepted >= 10)
    {
        remedy = QObject::tr("A single draw carries %1% of the total weight, so more candidate draws will not "
                             "help much.").arg(100.0 * largest, 0, 'f', 1) + " " + arbitrary;
    }
    else
    {
        remedy = QObject::tr("The weight tail is well behaved, so more candidate draws are the fix: the "
                             "effective sample size grows roughly in proportion to them.");
    }
    out << QObject::tr("The importance sample is unreliable: %1. %2").arg(detail.join(QObject::tr("; and ")), remedy);
    return out;
}

/// Every draws dataset below `group` (one column per draw, `draws` of them)
/// resampled by `index` -- bvartools' .resample_draws(). The labels start
/// afresh at one: a resample is not a chain.
void resamplePosterior(HighFive::File &h5, const ModelFile &mf, const std::string &group, int draws,
                       const std::vector<int> &index)
{
    const HighFive::Group g = h5.getGroup(group);
    for (const std::string &name : g.listObjectNames())
    {
        const std::string path = group + "/" + name;
        if (g.getObjectType(name) == HighFive::ObjectType::Group)
        {
            resamplePosterior(h5, mf, path, draws, index);
            continue;
        }
        HighFive::DataSet ds = h5.getDataSet(path);
        const auto dims = ds.getSpace().getDimensions();
        if (dims.size() != 2 || dims[1] != static_cast<std::size_t>(draws))
        {
            continue;
        }
        arma::mat raw(dims[1], dims[0]);   // row-major (rows, draws) is column-major (draws, rows)
        ds.read_raw(raw.memptr());
        arma::mat picked(index.size(), dims[0]);
        for (std::size_t i = 0; i < index.size(); ++i)
        {
            picked.row(i) = raw.row(static_cast<arma::uword>(index[i]));
        }
        h5.unlink(path);
        write_armadillo_matrix_to_hdf5(mf, path, picked, false);   // (n, rows) here is (rows, n) on disk
        HighFive::DataSet out = h5.getDataSet(path);
        out.createAttribute("start", 1.0);
        out.createAttribute("end", static_cast<double>(index.size()));
        out.createAttribute("thin", 1.0);
    }
}

} // namespace

std::vector<double> psisSmooth(const std::vector<double> &logWeights, double &paretoK)
{
    paretoK = std::nan("");
    std::vector<double> out(logWeights.size(), kNegInf);
    std::vector<std::size_t> keep;
    for (std::size_t i = 0; i < logWeights.size(); ++i)
    {
        if (std::isfinite(logWeights[i]))
        {
            keep.push_back(i);
        }
    }
    const auto n = static_cast<int>(keep.size());
    if (n == 0)
    {
        return out;
    }
    std::vector<double> lw(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i)
    {
        lw[static_cast<std::size_t>(i)] = logWeights[keep[static_cast<std::size_t>(i)]];
    }
    const double maxLw = *std::max_element(lw.begin(), lw.end());
    for (double &v : lw)
    {
        v -= maxLw;
    }
    const int tail = static_cast<int>(std::ceil(std::min(0.2 * n, 3.0 * std::sqrt(static_cast<double>(n)))));
    if (tail >= 5 && n > tail)
    {
        std::vector<int> order(static_cast<std::size_t>(n));
        std::iota(order.begin(), order.end(), 0);
        std::sort(order.begin(), order.end(), [&lw](int a, int b) { return lw[static_cast<std::size_t>(a)] < lw[static_cast<std::size_t>(b)]; });
        const int firstTail = n - tail;
        const double cutoff = lw[static_cast<std::size_t>(order[static_cast<std::size_t>(firstTail - 1)])];
        const double expCutoff = std::exp(cutoff);
        std::vector<double> excess(static_cast<std::size_t>(tail));
        for (int j = 0; j < tail; ++j)
        {
            excess[static_cast<std::size_t>(j)] = std::exp(lw[static_cast<std::size_t>(order[static_cast<std::size_t>(firstTail + j)])]) - expCutoff;
        }
        const bool degenerate = (excess.back() - excess.front()) < std::numeric_limits<double>::epsilon() * 100.0;
        if (!degenerate)
        {
            const GpdFit fit = fitGeneralisedPareto(excess);
            if (fit.ok)
            {
                paretoK = fit.k;
                for (int j = 0; j < tail; ++j)
                {
                    const double p = (static_cast<double>(j) + 0.5) / static_cast<double>(tail);
                    const double q = generalisedParetoQuantile(p, fit.k, fit.sigma) + expCutoff;
                    if (q > 0.0)
                    {
                        lw[static_cast<std::size_t>(order[static_cast<std::size_t>(firstTail + j)])] = std::log(q);
                    }
                }
            }
        }
    }
    for (double &v : lw)
    {
        v = std::min(v, 0.0);
    }
    const double normaliser = logSumExp(lw);
    for (int i = 0; i < n; ++i)
    {
        out[keep[static_cast<std::size_t>(i)]] = lw[static_cast<std::size_t>(i)] - normaliser;
    }
    return out;
}

std::vector<double> signZeroLogWeights(const QString &file, const std::vector<ZeroSignRestriction> &restrictions,
                                       std::uint64_t seed, int maxDraws)
{
    const detail::Posterior post(file);
    std::mt19937_64 rng(seed);
    const int m = static_cast<int>(post.fullCoefficients(0, -1).n_cols);
    const ArwSetup setup = arwSetup(restrictions, post.k(), m, post.levelLags(), rng);
    std::vector<double> out;
    for (int d = 0; d < std::min(maxDraws, post.draws()); ++d)
    {
        const ArwDraw draw = arwDraw(post.fullCoefficients(d, -1), post.system(d, -1).Sigma, setup, rng, false, 1);
        out.push_back(draw.q.n_elem > 0 ? draw.logWeight : kNegInf);
    }
    return out;
}

QString signZeroRefusal(const QString &file)
{
    try
    {
        const detail::Posterior post(file);
        if (post.structural())
        {
            return QObject::tr("A structural model has spent its identification on A_0; sign and zero "
                               "restrictions need the reduced form.");
        }
        const QString e = post.errorSpec();
        if (!(e == QLatin1String("wishart") || e.endsWith(QLatin1String("+covar")) || e.isEmpty()))
        {
            return QObject::tr("Sign and zero restrictions need estimated error covariances: Wishart errors, "
                               "or a covariance block with gamma or stochastic volatility errors.");
        }
    }
    catch (const std::exception &ex)
    {
        return QString::fromUtf8(ex.what());
    }
    return {};
}

void checkSignZeroRestrictions(const std::vector<ZeroSignRestriction> &restrictions, int k,
                               const QStringList &variables, bool requireZero)
{
    bool anyZero = false;
    std::map<std::tuple<int, int, double>, std::set<int>> cells;
    std::vector<int> zeros(static_cast<std::size_t>(k), 0);
    for (const auto &r : restrictions)
    {
        if (r.impulse < 0 || r.impulse >= k || r.response < 0 || r.response >= k)
        {
            throw std::runtime_error(QObject::tr("A restriction names a variable that does not exist.").toStdString());
        }
        if (r.sign != -1 && r.sign != 0 && r.sign != 1)
        {
            throw std::runtime_error(QObject::tr("A sign must be -1, 0 or 1.").toStdString());
        }
        if (!(r.horizon >= 0.0) || (std::isfinite(r.horizon) && r.horizon != std::round(r.horizon)))
        {
            throw std::runtime_error(
                QObject::tr("Horizons must be non-negative integers, or the long run.").toStdString());
        }
        anyZero = anyZero || r.sign == 0;
        cells[{r.impulse, r.response, r.horizon}].insert(r.sign);
        if (r.sign == 0)
        {
            ++zeros[static_cast<std::size_t>(r.impulse)];
        }
    }
    if (requireZero && !anyZero)
    {
        throw std::runtime_error(QObject::tr("There is no zero restriction, so sign restrictions alone draw the "
                                             "same rotations more cheaply.").toStdString());
    }
    for (const auto &cell : cells)
    {
        if (cell.second.size() > 1)
        {
            throw std::runtime_error(QObject::tr("Two restrictions ask for different things of the same response "
                                                 "at the same horizon.").toStdString());
        }
    }
    // Column j is drawn from what the j columns before it and its own zeros
    // leave; nothing left means the ordering has to change.
    for (int j = 0; j < k; ++j)
    {
        if (k - (j + zeros[static_cast<std::size_t>(j)]) < 1)
        {
            throw std::runtime_error(QObject::tr("The shock named after %1 carries %2 zero restrictions, which with "
                                                 "the %3 shocks before it leaves nothing of its column to draw. "
                                                 "Order the variables so that the shocks with the most zero "
                                                 "restrictions come first.")
                                         .arg(variables.value(j))
                                         .arg(zeros[static_cast<std::size_t>(j)])
                                         .arg(j)
                                         .toStdString());
        }
    }
}

SignZeroResult identifySignZero(const QString &file, const QString &output, const SignZeroSpec &spec,
                                std::uint64_t seed, const std::function<bool(int, int)> &progress)
{
    if (const QString why = signZeroRefusal(file); !why.isEmpty())
    {
        throw std::runtime_error(why.toStdString());
    }
    const detail::Posterior post(file);
    const int k = post.k();
    const QStringList variables =
        readStringsAttribute(file, QStringLiteral("/data/train/y"), QStringLiteral("variables"));
    checkSignZeroRestrictions(spec.restrictions, k, variables, true);
    if (spec.maxTries < 1)
    {
        throw std::runtime_error(QObject::tr("The tries per draw must be at least one.").toStdString());
    }
    const int store = post.draws();
    const int period = spec.period > 0 ? spec.period : -1;

    // The completions, the rotations and the resample are one stream.
    std::mt19937_64 rng(seed);
    const int m = static_cast<int>(post.fullCoefficients(0, period).n_cols);
    const ArwSetup setup = arwSetup(spec.restrictions, k, m, post.levelLags(), rng);

    const auto kk = static_cast<arma::uword>(k * k);
    arma::mat q(kk, static_cast<arma::uword>(store), arma::fill::value(arma::datum::nan));
    std::vector<double> logWeight(static_cast<std::size_t>(store), kNegInf);
    std::vector<double> logPHat(static_cast<std::size_t>(store), 0.0);
    SignZeroResult result;
    result.candidates = store;
    for (int d = 0; d < store; ++d)
    {
        if (progress && d % 10 == 0 && !progress(d, store))
        {
            throw std::runtime_error(QObject::tr("Cancelled.").toStdString());
        }
        const arma::mat coef = post.fullCoefficients(d, period);
        const arma::mat sigma = post.system(d, period).Sigma;
        const ArwDraw draw = arwDraw(coef, sigma, setup, rng, spec.oneSided, spec.maxTries);
        result.tries += draw.tries;
        if (draw.q.n_elem > 0)
        {
            q.col(static_cast<arma::uword>(d)) = arma::vectorise(draw.q);
            logWeight[static_cast<std::size_t>(d)] = draw.logWeight;
            logPHat[static_cast<std::size_t>(d)] = draw.logPHat;
            ++result.accepted;
        }
    }
    if (progress)
    {
        progress(store, store);
    }
    if (result.accepted == 0)
    {
        throw std::runtime_error(
            QObject::tr("No rotation satisfying the sign restrictions was found for any of the %1 posterior draws "
                        "with %2 tries each. With many sign restrictions a single try rarely satisfies them all; "
                        "allow more tries per draw. Otherwise every rotation the zero restrictions admit carries "
                        "the wrong signs.")
                .arg(store)
                .arg(spec.maxTries)
                .toStdString());
    }
    int unweighted = 0;
    for (double &lw : logWeight)
    {
        if (std::isnan(lw))
        {
            ++unweighted;
            lw = kNegInf;
        }
    }
    if (unweighted > 0)
    {
        result.warnings << QObject::tr("The importance weight of %1 of the %2 draws satisfying the sign "
                                       "restrictions could not be computed and they were dropped.")
                               .arg(unweighted)
                               .arg(result.accepted);
    }
    // Smoothed on the volume elements alone; the estimate of p multiplied in after.
    std::vector<double> logVolume = logWeight;
    bool anyPHat = false;
    for (std::size_t i = 0; i < logVolume.size(); ++i)
    {
        if (std::isfinite(logVolume[i]))
        {
            logVolume[i] -= logPHat[i];
            anyPHat = anyPHat || logPHat[i] != 0.0;
        }
    }
    std::vector<double> weights = arwWeights(logVolume, spec.smooth, result.paretoK);
    if (anyPHat)
    {
        double total = 0.0;
        for (std::size_t i = 0; i < weights.size(); ++i)
        {
            if (std::isfinite(logWeight[i]))
            {
                weights[i] *= std::exp(logPHat[i]);
            }
            total += weights[i];
        }
        for (double &w : weights)
        {
            w /= total;
        }
    }
    double sumSquares = 0.0;
    for (const double w : weights)
    {
        sumSquares += w * w;
        result.maxWeightShare = std::max(result.maxWeightShare, w);
    }
    result.effectiveSampleSize = std::max(1, static_cast<int>(std::floor(1.0 / sumSquares)));
    result.warnings << importanceWarnings(result.effectiveSampleSize, result.maxWeightShare, result.accepted,
                                          result.paretoK);
    result.draws = spec.draws > 0 ? spec.draws : result.effectiveSampleSize;
    std::discrete_distribution<int> pick(weights.begin(), weights.end());
    std::vector<int> index(static_cast<std::size_t>(result.draws));
    for (int &i : index)
    {
        i = pick(rng);
    }

    // The output: a copy whose whole posterior is the resample.
    if (QFile::exists(output) && !QFile::remove(output))
    {
        throw std::runtime_error(QObject::tr("Cannot replace %1.").arg(output).toStdString());
    }
    if (!QFile::copy(file, output))
    {
        throw std::runtime_error(QObject::tr("Cannot write %1.").arg(output).toStdString());
    }
    QFile::setPermissions(output, QFile::permissions(output) | QFileDevice::WriteOwner);
    HighFive::File h5(output.toStdString(), HighFive::File::ReadWrite);
    const ModelFile mf(h5);
    for (const char *old : {"/posterior/q", "/model/sign_restrictions", "/model/sign_zero_restrictions"})
    {
        if (h5.exist(old))
        {
            h5.unlink(old);
        }
    }
    resamplePosterior(h5, mf, "/posterior", store, index);
    arma::mat qPicked(index.size(), kk);
    for (std::size_t i = 0; i < index.size(); ++i)
    {
        qPicked.row(i) = arma::trans(q.col(static_cast<arma::uword>(index[i])));
    }
    h5.createGroup("/posterior/q");
    write_armadillo_matrix_to_hdf5(mf, "/posterior/q/coeffs", qPicked, false);   // (k*k, n) on disk
    {
        HighFive::DataSet ds = h5.getDataSet("/posterior/q/coeffs");
        ds.createAttribute("start", 1.0);
        ds.createAttribute("end", static_cast<double>(index.size()));
        ds.createAttribute("thin", 1.0);
    }

    // The tables, as bvartools' write_to_hdf5() writes sign_zero_restrictions
    // and sign_restrictions: (4, n) with a `columns` attribute.
    arma::mat table(spec.restrictions.size(), 4);
    for (std::size_t r = 0; r < spec.restrictions.size(); ++r)
    {
        const auto &x = spec.restrictions[r];
        table(r, 0) = x.impulse + 1;
        table(r, 1) = x.response + 1;
        table(r, 2) = x.sign;
        table(r, 3) = x.horizon;
    }
    const std::vector<std::string> columns{"impulse", "response", "sign", "horizon"};
    for (const char *groupName : {"/model/sign_zero_restrictions", "/model/sign_restrictions"})
    {
        HighFive::Group g = h5.createGroup(groupName);
        const std::string path = std::string(groupName) + "/restrictions";
        write_armadillo_matrix_to_hdf5(mf, path, table, false);
        h5.getDataSet(path).createAttribute("columns", columns);
        if (spec.period > 0)
        {
            g.createAttribute("period", std::vector<int>{spec.period});
        }
    }
    // bvartools builds before 2026-10-11 read max_tries from sign_restrictions
    // unconditionally; writing it there keeps them able to open the file.
    h5.getGroup("/model/sign_restrictions").createAttribute("max_tries", std::vector<int>{spec.maxTries});
    HighFive::Group g = h5.getGroup("/model/sign_zero_restrictions");
    g.createAttribute("one_sided", spec.oneSided);
    g.createAttribute("max_tries", std::vector<int>{spec.maxTries});
    g.createAttribute("candidates", std::vector<int>{result.candidates});
    g.createAttribute("tries", std::vector<int>{result.tries});
    g.createAttribute("accepted", std::vector<int>{result.accepted});
    g.createAttribute("effective_sample_size", std::vector<int>{result.effectiveSampleSize});
    g.createAttribute("max_weight_share", std::vector<double>{result.maxWeightShare});
    g.createAttribute("smooth", spec.smooth);
    g.createAttribute("pareto_k", std::vector<double>{result.paretoK});
    return result;
}

} // namespace studio
