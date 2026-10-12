// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Franz X. Mohr

#pragma once

#include <QString>

#include <optional>

namespace studio
{

/// The model comparison of the Evaluation stage, from what a run wrote.
///
/// WAIC (Watanabe 2010; Gelman, Hwang and Vehtari 2014) from the pointwise log
/// likelihood /posterior/loglik, one row per period and one column per draw:
/// lppd = sum_t log mean_d exp(l_td), p_waic = sum_t var_d(l_td), and
/// WAIC = -2 (lppd - p_waic), smaller being better.
///
/// The log predictive score is the out-of-sample counterpart, from
/// /posterior/forecast/loglik where the file carries /data/test/y: per horizon
/// the log of the posterior mean predictive density, summed.
struct Criteria
{
    int periods = 0;
    int draws = 0;
    std::optional<double> lppd;
    std::optional<double> pWaic;
    std::optional<double> waic;
    std::optional<double> predictiveScore;
    QString note; ///< why a criterion is missing
};

Criteria computeCriteria(const QString &file);

} // namespace studio
