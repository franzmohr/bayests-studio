// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Franz X. Mohr

#pragma once

#include <QString>
#include <QStringList>

#include <vector>

#include "bayests/arma.h"

namespace studio
{

/// The posterior of a path, period by period: the median and the bounds of
/// the central 68% and 90% intervals over the draws.
struct PathBands
{
    std::vector<double> lo90, lo68, median, hi68, hi90;
};

/// What the factor models' analysis reads from a file: the four DFMs and
/// FavarNormalWishart, estimated. The panel x_t of k series is measured on a
/// state s_t, x_t = Lambda_t s_t + C d_t + u_t, and the state follows a VAR,
/// s_t = sum_l A_{l,t} s_{t-l} + v_t. For a DFM the state is the factors; for
/// a FAVAR the unobserved factors and then the observed ones, /data/train/f_obs.
struct FactorSupport
{
    bool ok = false;
    QString reason;
    QString algorithm;
    bool favar = false;
    bool timeVarying = false;   ///< loadings and transition per period (DfmTvp*)
    bool stochvol = false;      ///< factor innovations' variances per period
    int k = 0;                  ///< series in the panel
    int factors = 0;            ///< unobserved factors
    int observedFactors = 0;    ///< a FAVAR's observed factors
    int p = 0;                  ///< the state VAR's order
    int periods = 0;
    int draws = 0;
    QStringList series;         ///< the panel's names
    QStringList state;          ///< the state's: "Factor 1", ..., then the observed factors
    QStringList observed;       ///< a FAVAR's observed factors' names
    std::vector<double> time;   ///< the date of each period, from R's tsp
    /// Impulse responses need a transition; a model of order zero has none.
    bool responses = false;
    QString responsesReason;
};

FactorSupport factorSupport(const QString &file);

/// The unobserved factors' paths, one PathBands per factor.
std::vector<PathBands> factorPaths(const QString &file);

/// The loadings at one period (the last where `period` is -1; a model whose
/// loadings do not move has one): k x state, the posterior median and the
/// bounds of the central 90% interval.
struct LoadingTable
{
    arma::mat median, lower, upper;
};

LoadingTable loadingTable(const QString &file, int period = -1);

/// The share of each series' variance its common component accounts for over
/// the sample, R^2 = 1 - var(x - C d - Lambda s) / var(x - C d), per draw;
/// summarised by the median and the central 90% interval.
struct ExplainedShare
{
    std::vector<double> median, lower, upper;
};

ExplainedShare explainedShare(const QString &file);

/// The responses to a one standard deviation shock to the state equation's
/// innovation `shock`, at horizons 0..horizon: one (horizon + 1) x (k + the
/// observed factors) matrix per draw, the panel's series first. The shocks are
/// orthogonal by construction in a DFM, its factor innovations independent;
/// a FAVAR's are the Choleski factor of their covariance, the observed factors
/// ordered last, as Bernanke, Boivin and Eliasz (2005). A model whose
/// coefficients or variances move uses those of `period` (-1: the last).
std::vector<arma::mat> factorResponses(const QString &file, int shock, int horizon, int period = -1);

/// Period by period quantiles over the draws of a stacked path, row
/// `t * width + row` of `draws` (one column per draw), for t = 0..periods-1.
PathBands summarisePath(const arma::mat &draws, int width, int row, int periods);

/// The /model/algorithm attribute, or an empty string.
QString modelAlgorithm(const QString &file);

/// A dataset as HDF5 stores it: for a posterior, one row per quantity and one
/// column per draw.
arma::mat readDraws(const QString &file, const QString &path);

/// /data/train/y as k x tt, whether it is stacked by period, (1, tt*k), or
/// one row per series, (k, tt).
arma::mat panelByPeriod(const QString &file, int k);

/// The date of each of `periods` periods from R's tsp of /data/train/y, or
/// 1, 2, ... without one.
std::vector<double> samplePeriodTimes(const QString &file, int periods);

} // namespace studio
