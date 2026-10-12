// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Franz X. Mohr

#pragma once

#include <QString>
#include <QStringList>

#include <limits>
#include <vector>

#include "bayests/arma.h"

namespace studio
{

/// The posterior draws of a VAR or VEC model file cut into the blocks of
/// bvartools' summary plot, plot.bvarmodel() and plot.bvecmodel(): one grid
/// per block, a row per equation and a column per regressor, so that panel
/// (i, j) carries the draws of the coefficient of regressor j in equation i.
///
///   VAR   lagged endogenous variables, exogenous variables, deterministic
///         terms, contemporaneous endogenous variables (structural), the
///         error covariance
///   VEC   the cointegration matrix Pi = alpha beta', lagged differences,
///         differenced exogenous variables, unrestricted deterministic terms,
///         contemporaneous endogenous variables, the error covariance
///
/// A model whose coefficients move gives a path per panel instead of draws,
/// and so does the covariance of a model whose covariance moves.
struct CoefficientPanel
{
    enum class Kind
    {
        Draws,      ///< `draws`, one per kept draw
        Path,       ///< `path`: periods x 3, the lower bound, median and upper bound
        Fixed,      ///< `value`, which every draw shares: A_0's ones and zeros
    };

    Kind kind = Kind::Draws;
    arma::vec draws;
    arma::mat path;
    double value = 0.0;

    /// Under variable selection, the posterior inclusion probability -- the
    /// mean of the coefficient's indicator draws, bvartools' summary()$a$lambda
    /// -- and whether selection reached the coefficient at all. One where it
    /// did not (a VEC's cointegration matrix, a term excluded from it, A_0's
    /// unit diagonal), zero above A_0's diagonal; NaN without selection.
    double inclusion = std::numeric_limits<double>::quiet_NaN();
    bool selected = false;
};

struct CoefficientBlock
{
    QString title;
    QStringList columns;                   ///< regressor names
    /// k x columns panels, column by column: panel (i, j) is panels[j * k + i].
    std::vector<CoefficientPanel> panels;
};

struct CoefficientSummary
{
    QString title;                         ///< "Bayesian TVP-SV-VAR model with p = 2"
    QStringList equations;                 ///< row names
    std::vector<CoefficientBlock> blocks;
    double start = 1.0;                    ///< the time of period 1, R's tsp
    double frequency = 1.0;
    int draws = 0;

    /// Variable selection: "bvs", "ssvs", or empty without it.
    QString selection;
    double priorInclusion = std::numeric_limits<double>::quiet_NaN();
    /// The covariance block's Psi (k x k, unit lower triangular) under its own
    /// selection: the inclusion probability of each element, as
    /// summary()$sigma$lambda, and which of them were selected over. Empty
    /// without it.
    arma::mat psiInclusion;
    arma::umat psiSelected;
};

/// The summary of `file`, paths summarised by the central `ci` interval
/// (bvartools' default 0.95). Throws with the reason where the model is not a
/// VAR or VEC with draws.
CoefficientSummary coefficientSummary(const QString &file, double ci = 0.95);

} // namespace studio
