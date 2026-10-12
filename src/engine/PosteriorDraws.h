// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Franz X. Mohr

#pragma once

// The posterior draws of a VAR or VEC model file, cut into one system per draw
// and period. Shared by the impulse responses (Structural.cpp) and the sign
// and zero restrictions (SignZero.cpp), so the two cannot disagree about which
// slice of a dataset belongs to a draw. Internal to the engine.

#include "engine/ModelFileTree.h"
#include "engine/ModelRunner.h"

#include <QObject>

#include <algorithm>
#include <stdexcept>
#include <vector>

#include "bayests/arma.h"

namespace studio::detail
{

/// One draw's system at the chosen period: the reduced form lag matrices
/// [A_1 .. A_p] (k x kp), and Sigma -- the reduced form error covariance, or
/// that of the structural errors for a structural model, whose A_0 comes too.
struct System
{
    arma::mat A;
    arma::mat Sigma;
    arma::mat A0;   ///< empty unless structural
};

/// The draws of a model file, and how to cut one system out of them.
class Posterior
{
public:
    explicit Posterior(const QString &file)
    {
        const CheckReport report = ModelRunner::check(file, QString());
        if (!report.ok)
        {
            throw std::runtime_error(report.error.toStdString());
        }
        if (!report.hasPosterior)
        {
            throw std::runtime_error(QObject::tr("The model has no posterior draws yet.").toStdString());
        }
        algorithm_ = report.algorithm;
        if (report.factors > 0 || algorithm_.startsWith(QLatin1String("Uc")))
        {
            throw std::runtime_error(
                QObject::tr("Impulse responses of factor and unobserved components models are not "
                            "available yet.").toStdString());
        }
        if (algorithm_.endsWith(QLatin1String("Ald")))
        {
            // The quantile VARs estimate no error covariance: their
            // u_sigma_inv holds the latent mixture precisions (bvartools'
            // .refuse_quantile_covariance()).
            throw std::runtime_error(
                QObject::tr("A quantile VAR estimates no error covariance to identify shocks from.").toStdString());
        }
        if (algorithm_.endsWith(QLatin1String("Discount")))
        {
            throw std::runtime_error(
                QObject::tr("The discounted models write a posterior per period, not draws; "
                            "impulse responses from them are not available yet.").toStdString());
        }
        errorSpec_ = report.errorSpec;
        k_ = report.k;
        p_ = report.p;
        rank_ = report.rank;
        kBeta_ = report.kBeta;
        structural_ = report.structural;
        tt_ = report.periods;
        vec_ = algorithm_.startsWith(QLatin1String("Vec"));
        m_ = readGroupInt(file, QStringLiteral("/model"), QStringLiteral("m"), 0);
        s_ = m_ > 0 ? readGroupInt(file, QStringLiteral("/model"), QStringLiteral("s"), 0) : 0;
        a_ = readMatrix(file, QStringLiteral("/posterior/a/coeffs"));
        sigmaInv_ = readMatrix(file, QStringLiteral("/posterior/u_sigma_inv/coeffs"));
        if (vec_)
        {
            beta_ = readMatrix(file, QStringLiteral("/posterior/beta/coeffs"));
        }
        const auto kk = static_cast<std::size_t>(k_ * k_);
        sigmaMoves_ = sigmaInv_.rows == kk * static_cast<std::size_t>(tt_) && tt_ > 1;
        aMoves_ = algorithm_.contains(QLatin1String("Tvp"));
        nparams_ = static_cast<int>(aMoves_ ? a_.rows / static_cast<std::size_t>(tt_) : a_.rows);
    }

    int draws() const { return static_cast<int>(a_.cols); }
    const QString &errorSpec() const { return errorSpec_; }

    /// The rotation of draw `d`, or an empty matrix where none was found.
    arma::mat rotation(const QString &file, int d) const
    {
        if (q_.rows == 0)
        {
            q_ = readMatrix(file, QStringLiteral("/posterior/q/coeffs"));
        }
        const auto k = static_cast<arma::uword>(k_);
        arma::mat Q(k, k);
        for (std::size_t i = 0; i < k * k; ++i)
        {
            Q(i % k, i / k) = q_.at(i, static_cast<std::size_t>(d));
        }
        return Q.has_nan() ? arma::mat() : Q;
    }
    int k() const { return k_; }
    bool structural() const { return structural_; }
    bool moving() const { return aMoves_ || sigmaMoves_; }
    int periods() const { return tt_; }
    /// The lag order as the file states it, a VEC's in levels.
    int lagOrder() const { return p_; }
    bool vec() const { return vec_; }

    System system(int draw, int period) const
    {
        const auto d = static_cast<std::size_t>(draw);
        const auto k = static_cast<arma::uword>(k_);
        const int t = (period < 1 || period > tt_) ? tt_ : period;
        const std::size_t aOff = aMoves_ ? static_cast<std::size_t>(t - 1) * nparams_ : 0;
        const auto coef = [&](std::size_t i) { return a_.at(aOff + i, d); };

        System s;
        const int nStruct = structural_ ? k_ * (k_ - 1) / 2 : 0;
        if (structural_)
        {
            // Unit lower triangular, the free elements column by column:
            // (2,1), (3,1), ..., (k,1), (3,2), ... -- the order of the
            // contemporaneous regressors (bvartools' collect_draws.R).
            s.A0 = arma::eye<arma::mat>(k, k);
            std::size_t pos = static_cast<std::size_t>(nparams_ - nStruct);
            for (arma::uword j = 0; j < k; ++j)
            {
                for (arma::uword i = j + 1; i < k; ++i)
                {
                    s.A0(i, j) = coef(pos++);
                }
            }
        }

        // The lag polynomial, less the identity a VEC's level form adds back.
        const int levelLags = std::max(p_, 1);
        arma::mat lags(k, k * static_cast<arma::uword>(levelLags), arma::fill::zeros);
        if (!vec_)
        {
            for (std::size_t i = 0; i < static_cast<std::size_t>(k_ * k_ * p_); ++i)
            {
                lags(i % k, i / k) = coef(i);
            }
        }
        else
        {
            // a = vec(alpha) (k x r), then vec([Gamma_1 .. Gamma_{p-1}, ...]);
            // beta's rows are y_{t-1} and then the restricted terms, which
            // the impulse response does not see.
            const auto r = static_cast<arma::uword>(rank_);
            arma::mat alpha(k, r);
            for (std::size_t i = 0; i < k * r; ++i)
            {
                alpha(i % k, i / k) = coef(i);
            }
            const std::size_t nBeta = static_cast<std::size_t>(kBeta_) * r;
            const bool betaMoves = beta_.rows == nBeta * static_cast<std::size_t>(tt_) && tt_ > 1;
            const std::size_t bOff = betaMoves ? static_cast<std::size_t>(t - 1) * nBeta : 0;
            arma::mat beta(static_cast<arma::uword>(kBeta_), r);
            for (std::size_t i = 0; i < nBeta; ++i)
            {
                beta(i % static_cast<std::size_t>(kBeta_), i / static_cast<std::size_t>(kBeta_)) =
                    beta_.at(bOff + i, d);
            }
            const arma::mat pi = alpha * arma::trans(beta.rows(0, k - 1));
            std::vector<arma::mat> gamma;
            const std::size_t gOff = k * r;
            for (int g = 0; g < p_ - 1; ++g)
            {
                arma::mat G(k, k);
                for (std::size_t i = 0; i < k * k; ++i)
                {
                    G(i % k, i / k) = coef(gOff + static_cast<std::size_t>(g) * k * k + i);
                }
                gamma.push_back(G);
            }
            // A_1 = I + Pi + Gamma_1, A_i = Gamma_i - Gamma_{i-1}, A_p = -Gamma_{p-1}.
            for (int i = 1; i <= levelLags; ++i)
            {
                arma::mat Ai = (i == 1) ? pi : arma::mat(k, k, arma::fill::zeros);
                if (i - 1 < static_cast<int>(gamma.size()))
                {
                    Ai += gamma[static_cast<std::size_t>(i - 1)];
                }
                if (i >= 2)
                {
                    Ai -= gamma[static_cast<std::size_t>(i - 2)];
                }
                lags.cols(static_cast<arma::uword>((i - 1) * k_), static_cast<arma::uword>(i * k_ - 1)) = Ai;
            }
        }
        if (structural_)
        {
            lags = arma::solve(s.A0, lags);
        }
        if (vec_)
        {
            lags.cols(0, k - 1) += arma::eye<arma::mat>(k, k);
        }
        s.A = lags;

        const std::size_t kk = k * k;
        const std::size_t sOff = sigmaMoves_ ? static_cast<std::size_t>(t - 1) * kk : 0;
        arma::mat precision(k, k);
        for (std::size_t i = 0; i < kk; ++i)
        {
            precision(i % k, i / k) = sigmaInv_.at(sOff + i, d);
        }
        s.Sigma = arma::inv_sympd(arma::symmatu(precision));
        return s;
    }


    /// Every coefficient of draw `draw` in the level VAR form, deterministic
    /// terms included: k x m, [A_1 .. A_p, C] for a VAR, and for a VEC
    /// [A_1 .. A_p, C, Pi_d] with Pi_d = alpha beta_d' the restricted terms --
    /// the order vec_to_var.h gives the level VAR. What bvartools'
    /// .collect_draws(all_regressors = TRUE) hands the sign and zero
    /// algorithm, whose importance weight depends on m. Refused for a
    /// structural model, which has no reduced form to rotate.
    arma::mat fullCoefficients(int draw, int period) const
    {
        const System s = system(draw, period);
        const auto d = static_cast<std::size_t>(draw);
        const auto k = static_cast<arma::uword>(k_);
        const int t = (period < 1 || period > tt_) ? tt_ : period;
        const std::size_t aOff = aMoves_ ? static_cast<std::size_t>(t - 1) * nparams_ : 0;
        const auto coef = [&](std::size_t i) { return a_.at(aOff + i, d); };
        if (!vec_)
        {
            const std::size_t nx = static_cast<std::size_t>(nparams_) / k;
            arma::mat full(k, nx);
            for (std::size_t i = 0; i < k * nx; ++i)
            {
                full(i % k, i / k) = coef(i);
            }
            return full;
        }
        // A VEC: after the Gamma blocks come Upsilon_0 .. Upsilon_{s-1} and
        // the unrestricted deterministic terms; the restricted ones are
        // Pi_d = alpha beta_d', beta's rows after y and x.
        const auto r = static_cast<arma::uword>(rank_);
        const auto m = static_cast<std::size_t>(m_);
        const std::size_t afterUpsilon = k * r + static_cast<std::size_t>(std::max(p_ - 1, 0)) * k * k +
                                         k * m * static_cast<std::size_t>(m_ > 0 ? s_ : 0);
        const std::size_t nUnrestricted = (static_cast<std::size_t>(nparams_) - afterUpsilon) / k;
        arma::mat c(k, nUnrestricted);
        for (std::size_t i = 0; i < k * nUnrestricted; ++i)
        {
            c(i % k, i / k) = coef(afterUpsilon + i);
        }
        const arma::mat alpha = loadings(d, aOff);
        const arma::mat beta = space(d, t);
        arma::mat restricted;
        const auto firstRestricted = static_cast<arma::uword>(k_ + m_);
        if (static_cast<arma::uword>(kBeta_) > firstRestricted)
        {
            restricted = alpha * arma::trans(beta.rows(firstRestricted, static_cast<arma::uword>(kBeta_) - 1));
        }
        return arma::join_rows(arma::join_rows(arma::join_rows(s.A, exogenousLevels(draw, period)), c), restricted);
    }

    int exogenous() const { return m_; }
    /// The exogenous lag order as the file states it, a VEC's in levels.
    int exogenousLags() const { return s_; }

    /// The level form's coefficients on the exogenous variables, [B_0 .. B_s]
    /// (k x m(s+1)), each block all m variables. A VAR stores them; a VEC's
    /// follow from vec_to_var.h: B_0 = Upsilon_0, B_1 = Pi_x - Upsilon_0 +
    /// Upsilon_1, B_j = Upsilon_j - Upsilon_{j-1}, B_s = -Upsilon_{s-1}, Pi_x
    /// = alpha beta_x'. Empty without exogenous variables.
    arma::mat exogenousLevels(int draw, int period) const
    {
        const auto k = static_cast<arma::uword>(k_);
        const auto m = static_cast<arma::uword>(m_);
        if (m == 0)
        {
            return arma::mat(k, 0);
        }
        const auto d = static_cast<std::size_t>(draw);
        const int t = (period < 1 || period > tt_) ? tt_ : period;
        const std::size_t aOff = aMoves_ ? static_cast<std::size_t>(t - 1) * nparams_ : 0;
        const auto coef = [&](std::size_t i) { return a_.at(aOff + i, d); };
        const auto blocks = static_cast<arma::uword>(s_ + 1);
        arma::mat B(k, m * blocks, arma::fill::zeros);
        if (!vec_)
        {
            const std::size_t first = k * k * static_cast<std::size_t>(p_);
            for (std::size_t i = 0; i < k * m * blocks; ++i)
            {
                B(i % k, i / k) = coef(first + i);
            }
            return B;
        }
        const auto r = static_cast<arma::uword>(rank_);
        const std::size_t first = k * r + static_cast<std::size_t>(std::max(p_ - 1, 0)) * k * k;
        arma::mat ups(k, m * static_cast<arma::uword>(s_));
        for (std::size_t i = 0; i < ups.n_elem; ++i)
        {
            ups(i % k, i / k) = coef(first + i);
        }
        const auto block = [&](const arma::mat &x, int j) {
            return x.cols(static_cast<arma::uword>(j) * m, static_cast<arma::uword>(j + 1) * m - 1);
        };
        for (int j = 0; j <= s_; ++j)
        {
            arma::mat Bj(k, m, arma::fill::zeros);
            if (j < s_)
            {
                Bj += block(ups, j);
            }
            if (j >= 1)
            {
                Bj -= block(ups, j - 1);
            }
            B.cols(static_cast<arma::uword>(j) * m, static_cast<arma::uword>(j + 1) * m - 1) = Bj;
        }
        if (rank_ > 0)
        {
            const arma::mat piX = loadings(d, aOff) * arma::trans(space(d, t).rows(k, k + m - 1));
            B.cols(m, 2 * m - 1) += piX;
        }
        return B;
    }

    /// The lags of the level form.
    int levelLags() const { return std::max(p_, 1); }
    const QString &algorithm() const { return algorithm_; }

private:
    /// alpha (k x r) of draw d, its period's coefficients starting at aOff.
    arma::mat loadings(std::size_t d, std::size_t aOff) const
    {
        const auto k = static_cast<arma::uword>(k_);
        const auto r = static_cast<arma::uword>(rank_);
        arma::mat alpha(k, r);
        for (std::size_t i = 0; i < k * r; ++i)
        {
            alpha(i % k, i / k) = a_.at(aOff + i, d);
        }
        return alpha;
    }

    /// beta (k_beta x r) of draw d at period t (1-based).
    arma::mat space(std::size_t d, int t) const
    {
        const auto r = static_cast<std::size_t>(rank_);
        const auto kb = static_cast<std::size_t>(kBeta_);
        const std::size_t nBeta = kb * r;
        const bool betaMoves = beta_.rows == nBeta * static_cast<std::size_t>(tt_) && tt_ > 1;
        const std::size_t bOff = betaMoves ? static_cast<std::size_t>(t - 1) * nBeta : 0;
        arma::mat beta(kb, r);
        for (std::size_t i = 0; i < nBeta; ++i)
        {
            beta(i % kb, i / kb) = beta_.at(bOff + i, d);
        }
        return beta;
    }

    QString algorithm_;
    QString errorSpec_;
    mutable Matrix q_;
    int m_ = 0, s_ = 0;
    int k_ = 0, p_ = 0, rank_ = 0, kBeta_ = 0, tt_ = 0, nparams_ = 0;
    bool structural_ = false, vec_ = false, aMoves_ = false, sigmaMoves_ = false;
    Matrix a_, sigmaInv_, beta_;
};


} // namespace studio::detail
