// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Franz X. Mohr

#pragma once

#include <QString>
#include <QStringList>

#include <vector>

#include "bayests/arma.h"

namespace studio
{

/// Dynamic multipliers: the response of the endogenous variables to a change
/// in one weakly exogenous variable, as bvartools' multipliers() computes them
/// (R/multipliers.bvarmodel.R), so the two give the same numbers from the same
/// draws. From the level form
///
///   y_t = sum_l A_l y_{t-l} + sum_j B_j x_{t-j} + C d_t + u_t,
///
///   M_h = sum_{l=1}^{min(h,p)} A_l M_{h-l} + sum_{j=0}^{min(h,s)} B_j e   (permanent)
///   M_h = sum_{l=1}^{min(h,p)} A_l M_{h-l} + B_h e  for h <= s            (transitory)
///
/// draw by draw, times the size of the change. A VEC is read as the level VAR
/// its draws imply, which is where its cointegration relations move the levels
/// for good. The deterministic terms and the errors cancel.
struct MultiplierRequest
{
    int impulse = 0;          ///< the exogenous variable moved, 0-based
    int horizon = 20;
    double shock = 1.0;       ///< in the exogenous variable's own units
    bool permanent = true;    ///< held from period zero on, or period zero alone
    bool cumulative = false;
    int period = -1;          ///< 1-based estimation period; -1 is the last
    int maxDraws = 4000;      ///< thinned evenly to at most this many
};

struct MultiplierSupport
{
    bool ok = false;
    QString reason;           ///< why not, when !ok
    int k = 0;
    int m = 0;
    bool moving = false;      ///< coefficients per period
    int periods = 0;
    QStringList variables;    ///< the endogenous variables, in levels
    QStringList exogenous;    ///< the exogenous variables
};

MultiplierSupport multiplierSupport(const QString &file);

/// Every draw's multipliers: draws[d] is (horizon + 1) x k, rows horizons and
/// columns responses.
std::vector<arma::mat> dynamicMultipliers(const QString &file, const MultiplierRequest &request);

} // namespace studio
