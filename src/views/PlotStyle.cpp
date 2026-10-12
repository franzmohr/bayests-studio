// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Franz X. Mohr

#include "views/PlotStyle.h"
#include "views/Fonts.h"
#include "views/Palettes.h"

#include <QApplication>
#include <QFont>
#include <QPalette>

#include <qwt_plot.h>
#include <qwt_plot_canvas.h>
#include <qwt_plot_grid.h>
#include <qwt_text.h>

#include <array>

namespace studio
{

QColor seriesColor(int index, int alpha, int count)
{
    QColor c = currentPalette().colour(index, count);
    c.setAlpha(alpha);
    return c;
}

void styleAxes(QwtPlot *plot, const QString &xTitle, const QString &yTitle)
{
    const QPalette pal = QApplication::palette();

    auto *canvas = new QwtPlotCanvas;
    canvas->setFrameStyle(QFrame::NoFrame);
    canvas->setBorderRadius(0);
    plot->setCanvas(canvas);
    plot->setCanvasBackground(pal.color(QPalette::Base));
    plot->setAutoFillBackground(true);
    plot->setPalette(pal);

    // The figure font's family, at the size the interface reads at.
    const QFont axisFont = figureFont(plot->font().pointSizeF() * 0.9);
    for (const auto axis : {QwtPlot::xBottom, QwtPlot::yLeft})
    {
        plot->setAxisFont(axis, axisFont);
    }
    QwtText xt(xTitle);
    xt.setFont(axisFont);
    QwtText yt(yTitle);
    yt.setFont(axisFont);
    plot->setAxisTitle(QwtPlot::xBottom, xt);
    plot->setAxisTitle(QwtPlot::yLeft, yt);

    auto *grid = new QwtPlotGrid;
    QColor gridColor = pal.color(QPalette::Text);
    gridColor.setAlpha(28);
    grid->setMajorPen(gridColor, 0.0, Qt::SolidLine);
    grid->attach(plot);
}

} // namespace studio
