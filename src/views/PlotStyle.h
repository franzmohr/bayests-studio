#pragma once

#include <QColor>
#include <QString>

class QwtPlot;

namespace studio
{

/// The categorical palette every plot draws its series from, in order.
QColor seriesColor(int index, int alpha = 255);

/// Common look for a plot: flat canvas, light grid, axis titles, palette-aware
/// colours so plots follow the application's light or dark theme.
void styleAxes(QwtPlot *plot, const QString &xTitle, const QString &yTitle);

} // namespace studio
