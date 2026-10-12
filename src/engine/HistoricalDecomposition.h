// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Franz X. Mohr

#pragma once

#include <QString>
#include <QStringList>

#include <vector>

#include "bayests/arma.h"

namespace studio
{

/// Historical decompositions as bvartools' historical_decomposition()
/// computes them (R/historical_decomposition.R), so the two give the same
/// numbers from the same draws. For each draw the structural shocks of the
/// sample are e_t = P^-1 u_t, u_t the residuals of the level form with every
/// regressor, and their effects are propagated through the sample at once,
///
///   M_t = sum_l A_l M_{t-l} + P diag(e_t),
///
/// column j of M_t being what shock j has done to y_t so far. The baseline is
/// what the shocks leave of the data: the deterministic terms, the exogenous
/// variables and the initial conditions. A VEC is decomposed in its level
/// form, from the levels its data imply. Constant coefficients and a constant
/// error covariance only, and no structural model -- as bvartools.
enum class HdType
{
    Oir,    ///< Choleski shocks
    Sign,   ///< chol(Sigma)' Q, the rotations a sign identification stored
};

struct HdRequest
{
    HdType type = HdType::Oir;
    int response = 0;
    bool median = false;      ///< the posterior median rather than the mean
    double ci = 0.9;          ///< the central interval of the bounds
    int maxDraws = 1 << 30;   ///< thinned evenly to at most this many
};

struct HdSupport
{
    bool ok = false;
    QString reason;
    int k = 0;
    int periods = 0;
    QStringList variables;    ///< in levels
    bool sign = false;        ///< carries a sign identification
    double start = 1.0;       ///< R's tsp of the estimation sample
    double frequency = 1.0;
};

HdSupport hdSupport(const QString &file);

struct HistoricalDecomposition
{
    /// tt x (k + 1): each shock's contribution to the response, then the
    /// baseline. `centre` is the mean or median, `lower` and `upper` the bounds.
    arma::mat centre, lower, upper;
    arma::vec data;           ///< the response, in levels for a VEC
    std::vector<double> time; ///< the date of each period
    int draws = 0;
};

HistoricalDecomposition historicalDecomposition(const QString &file, const HdRequest &request);

} // namespace studio
