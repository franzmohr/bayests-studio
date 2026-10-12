#pragma once

#include <QString>
#include <QStringList>

#include <vector>

#include "bayests/arma.h"

namespace studio
{

/// The posterior of a volatility break (Lenza and Primiceri 2022), as
/// VarNormalWishart and VecNormalWishart write it under
/// /model/volatility_break: the draws of the `m` free scales and of the decay
/// rate rho, and the path of the scale they imply,
///
///   s_t = 1                                   before the break,
///   s_t = the j-th free scale                 the j-th period from it, j < m,
///   s_t = 1 + (s_{m-1} - 1) rho^(j - m + 1)   after them,
///
/// over the sample and the forecast horizon, whose shocks the forecast draws
/// with s_{T+i}.
struct VolatilityBreakSummary
{
    int breakColumn = 0;     ///< the first scaled column of /data/train/y, from one
    int scales = 0;          ///< m
    int periods = 0;         ///< tt
    int horizon = 0;         ///< h
    int draws = 0;
    double start = 1.0;      ///< R's tsp of the estimation sample
    double frequency = 1.0;
    bool dated = false;

    /// The parameters, the free scales then rho: one row each.
    QStringList labels;                  ///< "s 2020-03", ..., "ρ"
    arma::mat parameters;                ///< (m + 1) x draws
    /// Per parameter: mean, sd, and the 5, 16, 50, 84 and 95% quantiles.
    arma::mat table;                     ///< (m + 1) x 7

    /// The path from a few periods before the break to the end of the
    /// horizon: its time, and the 5, 16, 50, 84 and 95% quantiles of s_t.
    std::vector<double> time;
    arma::mat path;                      ///< periods x 5
    int firstColumn = 1;                 ///< the column of /data/train/y path row 0 is
    double sampleEnd = 0.0;              ///< the time of the last sample period

    // The prior: a Pareto with scale one on every free scale, a beta on rho.
    double shape = 0.0;
    double rhoShape1 = 0.0;
    double rhoShape2 = 0.0;

    /// The time of column `c` of /data/train/y, from one.
    double timeOf(int c) const { return dated ? start + (c - 1) / frequency : c; }
    /// The label of column `c`: "2020-03", "2020 Q2", or the column number.
    QString labelOf(int c) const;
};

/// Whether `file` holds the draws of a volatility break.
bool hasVolatilityBreak(const QString &file);

/// Reads them. Throws when the file has none.
VolatilityBreakSummary volatilityBreakSummary(const QString &file);

/// The scale in the `j`-th period from the break (j >= 0) of one draw: the
/// free scales, then rho. Below zero it is one.
double breakScale(const arma::vec &draw, int scales, int j);

} // namespace studio
