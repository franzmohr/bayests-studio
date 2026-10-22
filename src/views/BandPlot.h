// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Franz X. Mohr

#pragma once

#include "engine/FactorAnalysis.h"

#include <QString>

#include <vector>

class QwtPlot;

namespace studio
{

/// Removes every curve, band and marker from `plot`.
void clearBandPlot(QwtPlot *plot);

/// A path over dated periods, `x`: its posterior median as a line and the 68%
/// and 90% bands around it, in palette colour `color`. A zero line first where
/// `zero` is set. The caller replots.
void drawPathBands(QwtPlot *plot, const std::vector<double> &x, const PathBands &bands, int color, bool zero = false);

/// A plain line over `x` -- the data a band is drawn against.
void drawPathLine(QwtPlot *plot, const std::vector<double> &x, const std::vector<double> &y, int color,
                  const QString &name);

} // namespace studio
