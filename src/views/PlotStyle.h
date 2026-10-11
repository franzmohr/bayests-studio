#pragma once

#include <QColor>
#include <QString>

class QwtPlot;

namespace studio
{

/// Colour `index` of the palette the user chose (views/Palettes.h), which
/// every plot draws its series from; `count`, where the plot knows it, is the
/// number of series, which a palette such as ggplot2's hue depends on.
QColor seriesColor(int index, int alpha = 255, int count = 0);

/// Common look for a plot: flat canvas, light grid, axis titles, palette-aware
/// colours so plots follow the application's light or dark theme.
void styleAxes(QwtPlot *plot, const QString &xTitle, const QString &yTitle);

} // namespace studio
