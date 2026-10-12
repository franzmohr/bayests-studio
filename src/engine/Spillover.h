// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Franz X. Mohr

#pragma once

#include <QString>
#include <QStringList>

#include <functional>
#include <vector>

#include "bayests/arma.h"

namespace studio
{

/// The connectedness measures of Diebold and Yilmaz (2012, 2014) from the
/// posterior draws in a model file, as bvartools' spillover() computes them
/// (R/spillover.bvarmodel.R, src/spillover_table.cpp), so the two give the
/// same numbers from the same draws.
///
/// theta_jk(H) is the share of the H step forecast error variance of j -- made
/// of the responses of periods 0 to H - 1 -- attributed to a shock to k. Under
/// the generalised decomposition (Pesaran and Shin 1998) the numerator is
/// divided by the variance of the *shock* variable and the rows, which do not
/// add up, are normalised; the orthogonalised and sign restricted ones add up
/// already. Every measure is computed per draw and only then summarised.
enum class SpilloverType
{
    Gir,    ///< generalised: invariant to the ordering, Diebold and Yilmaz (2012)
    Oir,    ///< Choleski: depends on the ordering
    Sign,   ///< chol(Sigma)' Q, the rotations a sign identification stored
};

QString spilloverTypeName(SpilloverType type);

struct SpilloverRequest
{
    SpilloverType type = SpilloverType::Gir;
    int horizon = 10;       ///< H, the horizon of Diebold and Yilmaz (2012)
    int period = -1;        ///< 1-based estimation period; -1 is the last
    int maxDraws = 4000;    ///< thinned evenly to at most this many
};

/// What the model offers, decided from the file.
struct SpilloverSupport
{
    bool ok = false;
    QString reason;          ///< why not, when !ok
    bool moving = false;     ///< coefficients or covariance per period
    int periods = 0;
    int k = 0;
    QStringList variables;
    bool sign = false;       ///< carries a sign identification
    int signPeriod = -1;     ///< the period its rotations were found at
    /// The estimation sample's time index, R's tsp of /data/train/y:
    /// period t is at start + (t - 1) / frequency. 1, 2, ... without one.
    double start = 1.0;
    double frequency = 1.0;
};

SpilloverSupport spilloverSupport(const QString &file);

/// The normalised table of one system: k x k, row j the shares of the H step
/// forecast error variance of j, each row summing to one. `A` is the lag
/// polynomial [A_1 .. A_p] (k x kp), `P` the impact matrix, `generalised` the
/// Pesaran and Shin scaling by the shock's variance. bvartools'
/// .spillover_table().
arma::mat connectednessTable(const arma::mat &A, const arma::mat &Sigma, const arma::mat &P, int horizon,
                             bool generalised);

/// Every draw's measures, in percent as bvartools reports them.
struct Spillovers
{
    int period = -1;        ///< the period read
    arma::vec total;        ///< draws
    arma::mat from;         ///< draws x k: received from all others
    arma::mat to;           ///< draws x k: transmitted to all others
    arma::mat net;          ///< to - from
    arma::mat table;        ///< k x k posterior mean of the normalised table, percent
    arma::mat pairwise;     ///< k x k posterior mean net pairwise, (theta_kj - theta_jk) / k
};

Spillovers spillovers(const QString &file, const SpilloverRequest &request);

/// The total index at every period of a model whose coefficients or
/// covariance move: the connectedness of the system as the model estimates it
/// in each period, which is what Diebold and Yilmaz obtain from rolling
/// windows and a time-varying model gives from one estimation. quantiles is
/// periods x 5 -- the 5, 16, 50, 84 and 95 percentiles. Refused for a sign
/// identification, whose rotations identify one period only.
///
/// The table's entries over the sample come with it: entries[j * k + l] is
/// periods x 5, the same percentiles of theta_jl(H) in percent -- the share of
/// the forecast error variance of j due to a shock to l -- and table.slice(t)
/// the posterior mean table of period t + 1, which is Spillovers::table there.
struct SpilloverPath
{
    std::vector<double> time;
    arma::mat quantiles;
    std::vector<arma::mat> entries;
    arma::cube table;
    int draws = 0;
};

SpilloverPath spilloverPath(const QString &file, const SpilloverRequest &request,
                            const std::function<bool(int, int)> &progress = {});

} // namespace studio
