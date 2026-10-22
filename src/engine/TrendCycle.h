// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Franz X. Mohr

#pragma once

#include "engine/FactorAnalysis.h"

#include <QString>
#include <QStringList>

#include <vector>

namespace studio
{

/// What the unobserved components models' analysis reads from a file: the
/// four Uc* models, estimated. Each series is y_t = tau_t + c_t (+ eps_t with
/// an irregular component): /posterior/trend/coeffs holds the trend path, and
/// the cycle is either /posterior/cycle/coeffs, drawn with the trend where the
/// model has an irregular component, or the data less the trend.
struct TrendCycleSupport
{
    bool ok = false;
    QString reason;
    QString algorithm;
    bool irregular = false;
    int k = 0;
    int periods = 0;
    int draws = 0;
    QStringList series;
    std::vector<double> time;   ///< the date of each period, from R's tsp
};

TrendCycleSupport trendCycleSupport(const QString &file);

struct TrendCycleSeries
{
    std::vector<double> data;
    PathBands trend, cycle;
};

/// One entry per series, in the order of /data/train/y.
std::vector<TrendCycleSeries> trendCycle(const QString &file);

} // namespace studio
