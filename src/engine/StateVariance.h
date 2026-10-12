// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Franz X. Mohr

#pragma once

#include <QString>

#include <vector>

namespace studio
{

/// The prior on the random walk variances of a time-varying VAR's or VEC's
/// coefficients, /priors/a, as far as the state variance explorer needs it.
///
/// The centred prior is an inverse gamma per coefficient, `shape` and `rate`,
/// whose mean precision is shape / rate -- the builder starts the chain at
/// rate / shape, and that is the variance a run is said to be "at". The
/// non-centred one (Frühwirth-Schnatter and Wagner 2010) is a normal on the
/// signed standard deviation omega, whose variance `omega_v` is then the level.
struct StateVariancePrior
{
    bool available = false;
    QString refusal;                ///< why not, when available is false
    bool nonCentred = false;
    int coefficients = 0;           ///< rows of /priors/a
    int periods = 0;
    int iterations = 0, burnin = 0;
    std::vector<double> level;      ///< rate / shape, or omega_v, per coefficient
    double typical = 0.0;           ///< the median of `level`
};

StateVariancePrior readStateVariancePrior(const QString &file);

/// One re-estimation: every coefficient's level scaled by variance / typical,
/// so that the typical one is at `variance` and the constant and trend keep
/// their ratio to it. `fixed` makes the centred prior so tight (shape 10^6)
/// that the variance is in effect set rather than estimated.
struct StateVarianceRun
{
    double variance = 0.0;
    bool fixed = false;
    int iterations = 0, burnin = 0;
};

/// Copies `file` to `output` with the run's prior, its starting state
/// precision and its chain length, and without the posterior. The seed is
/// kept, so runs differ by the prior and not by the random numbers.
void writeStateVarianceVariant(const QString &file, const QString &output, const StateVarianceRun &run);

/// One coefficient's path and state standard deviation over the draws.
struct StatePathSummary
{
    std::vector<double> lo90, lo68, median, hi68, hi90;   ///< per period
    std::vector<double> stateSd;                          ///< sqrt of /posterior/a/sigma, per draw, sorted
    int draws = 0;
};

/// Reads /posterior/a/coeffs (the path stacked period by period) and
/// /posterior/a/sigma of an estimated file. Throws when they are missing.
StatePathSummary summariseStatePath(const QString &file, int coefficient, int coefficients);

} // namespace studio
