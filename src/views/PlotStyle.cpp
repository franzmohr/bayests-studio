#include "views/PlotStyle.h"

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

QColor seriesColor(int index, int alpha)
{
    // Distinguishable in both themes and under the common colour-vision
    // deficiencies (Okabe–Ito ordering, blue first).
    static const std::array<QColor, 6> palette = {
        QColor(0x2F, 0x6D, 0xB5), QColor(0xD5, 0x5E, 0x00), QColor(0x00, 0x9E, 0x73),
        QColor(0xCC, 0x79, 0xA7), QColor(0xE6, 0x9F, 0x00), QColor(0x56, 0xB4, 0xE9),
    };
    QColor c = palette[static_cast<std::size_t>(index) % palette.size()];
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

    QFont axisFont = plot->font();
    axisFont.setPointSizeF(axisFont.pointSizeF() * 0.9);
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
